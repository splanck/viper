//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_shift_reg.cpp
// Purpose: Verify shift by register (variable shift amount) on AArch64.
// Key invariants: Emits lslv/lsrv/asrv instructions for register shifts.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "tools/viper/cmd_codegen_arm64.hpp"

using namespace viper::tools::ilc;

static std::string outPath(const std::string &name)
{
    namespace fs = std::filesystem;
    const fs::path dir{"build/test-out/arm64"};
    fs::create_directories(dir);
    return (dir / name).string();
}

static void writeFile(const std::string &path, const std::string &text)
{
    std::ofstream ofs(path);
    ASSERT_TRUE(static_cast<bool>(ofs));
    ofs << text;
}

static std::string readFile(const std::string &path)
{
    std::ifstream ifs(path);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

// Test 1: Logical left shift by register
TEST(Arm64ShiftReg, ShlByReg)
{
    const std::string in = outPath("arm64_shl_reg.il");
    const std::string out = outPath("arm64_shl_reg.s");
    const std::string il = "il 0.1\n"
                           "func @shl(%val:i64, %amt:i64) -> i64 {\n"
                           "entry(%val:i64, %amt:i64):\n"
                           "  %r = shl %val, %amt\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect lslv (or lsl with reg operand) for variable shift
    bool hasShift =
        asmText.find("lslv x") != std::string::npos || asmText.find("lsl x") != std::string::npos;
    EXPECT_TRUE(hasShift);
}

// Test 2: Logical right shift by register
TEST(Arm64ShiftReg, LshrByReg)
{
    const std::string in = outPath("arm64_lshr_reg.il");
    const std::string out = outPath("arm64_lshr_reg.s");
    const std::string il = "il 0.1\n"
                           "func @lshr(%val:i64, %amt:i64) -> i64 {\n"
                           "entry(%val:i64, %amt:i64):\n"
                           "  %r = lshr %val, %amt\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect lsrv (or lsr with reg operand) for variable shift
    bool hasShift =
        asmText.find("lsrv x") != std::string::npos || asmText.find("lsr x") != std::string::npos;
    EXPECT_TRUE(hasShift);
}

// Test 3: Arithmetic right shift by register
TEST(Arm64ShiftReg, AshrByReg)
{
    const std::string in = outPath("arm64_ashr_reg.il");
    const std::string out = outPath("arm64_ashr_reg.s");
    const std::string il = "il 0.1\n"
                           "func @ashr(%val:i64, %amt:i64) -> i64 {\n"
                           "entry(%val:i64, %amt:i64):\n"
                           "  %r = ashr %val, %amt\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect asrv (or asr with reg operand) for variable shift
    bool hasShift =
        asmText.find("asrv x") != std::string::npos || asmText.find("asr x") != std::string::npos;
    EXPECT_TRUE(hasShift);
}

// Test 4: Shift where amount comes from computation
TEST(Arm64ShiftReg, ShiftFromComputation)
{
    const std::string in = outPath("arm64_shift_computed.il");
    const std::string out = outPath("arm64_shift_computed.s");
    const std::string il = "il 0.1\n"
                           "func @shift_computed(%val:i64, %base:i64) -> i64 {\n"
                           "entry(%val:i64, %base:i64):\n"
                           "  %amt = add %base, 1\n"
                           "  %r = shl %val, %amt\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have add and shift
    EXPECT_NE(asmText.find("add x"), std::string::npos);
    bool hasShift =
        asmText.find("lslv x") != std::string::npos || asmText.find("lsl x") != std::string::npos;
    EXPECT_TRUE(hasShift);
}

// Test 5: All three shift types in one function
TEST(Arm64ShiftReg, AllShifts)
{
    const std::string in = outPath("arm64_all_shifts.il");
    const std::string out = outPath("arm64_all_shifts.s");
    const std::string il = "il 0.1\n"
                           "func @all_shifts(%v:i64, %a:i64) -> i64 {\n"
                           "entry(%v:i64, %a:i64):\n"
                           "  %t1 = shl %v, %a\n"
                           "  %t2 = lshr %t1, %a\n"
                           "  %t3 = ashr %t2, %a\n"
                           "  ret %t3\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should compile successfully
    EXPECT_FALSE(asmText.empty());
}

// Test 6: Shift with masked amount (common pattern)
TEST(Arm64ShiftReg, ShiftMaskedAmount)
{
    const std::string in = outPath("arm64_shift_masked.il");
    const std::string out = outPath("arm64_shift_masked.s");
    // Mask shift amount to 6 bits (0-63 for 64-bit)
    const std::string il = "il 0.1\n"
                           "func @shift_masked(%val:i64, %amt:i64) -> i64 {\n"
                           "entry(%val:i64, %amt:i64):\n"
                           "  %masked = and %amt, 63\n"
                           "  %r = shl %val, %masked\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have and instruction (or optimized away if shift handles masking)
    EXPECT_FALSE(asmText.empty());
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
