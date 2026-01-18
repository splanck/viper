//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_call_convention.cpp
// Purpose: Comprehensive tests for AArch64 integer calling convention.
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

/// @brief Returns the expected mangled symbol name for a call target.
/// On Darwin (macOS), symbols are prefixed with underscore.
static std::string blSym(const std::string &name)
{
#if defined(__APPLE__)
    return "bl _" + name;
#else
    return "bl " + name;
#endif
}

// Test 1: Simple add3(a, b, c) helper
TEST(Arm64CallConv, Add3Helper)
{
    const std::string in = outPath("arm64_call_add3.il");
    const std::string out = outPath("arm64_call_add3.s");
    const std::string il = "il 0.1\n"
                           "func @add3(%a:i64, %b:i64, %c:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64, %c:i64):\n"
                           "  %t1 = add %a, %b\n"
                           "  %t2 = add %t1, %c\n"
                           "  ret %t2\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect add instructions
    EXPECT_NE(asmText.find("add x"), std::string::npos);
}

// Test 2: Caller passes computed values to add3
TEST(Arm64CallConv, CallerWithComputedArgs)
{
    const std::string in = outPath("arm64_call_computed.il");
    const std::string out = outPath("arm64_call_computed.s");
    const std::string il = "il 0.1\n"
                           "extern @add3(i64, i64, i64) -> i64\n"
                           "func @caller(%x:i64, %y:i64) -> i64 {\n"
                           "entry(%x:i64, %y:i64):\n"
                           "  %a = mul %x, 2\n"
                           "  %b = add %y, 1\n"
                           "  %c = sub %x, %y\n"
                           "  %r = call @add3(%a, %b, %c)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect arithmetic operations before the call
    // mul * 2 may be strength-reduced to lsl #1, so accept either
    EXPECT_TRUE(asmText.find("mul x") != std::string::npos ||
                asmText.find("lsl x") != std::string::npos);
    EXPECT_NE(asmText.find("add x"), std::string::npos);
    EXPECT_NE(asmText.find("sub x"), std::string::npos);
    EXPECT_NE(asmText.find(blSym("add3")), std::string::npos);
}

// Test 3: Call result stored to local and reused
TEST(Arm64CallConv, CallResultStoredAndReused)
{
    const std::string in = outPath("arm64_call_reuse.il");
    const std::string out = outPath("arm64_call_reuse.s");
    const std::string il = "il 0.1\n"
                           "extern @twice(i64) -> i64\n"
                           "func @f(%a:i64) -> i64 {\n"
                           "entry(%a:i64):\n"
                           "  %L = alloca 8\n"
                           "  %c = call @twice(%a)\n"
                           "  store i64, %L, %c\n"
                           "  %v = load i64, %L\n"
                           "  %r = add %v, 10\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find(blSym("twice")), std::string::npos);
    EXPECT_NE(asmText.find("str x"), std::string::npos);
    EXPECT_NE(asmText.find("ldr x"), std::string::npos);
}

// Test 4: Multiple calls in sequence
TEST(Arm64CallConv, MultipleCalls)
{
    const std::string in = outPath("arm64_multi_call.il");
    const std::string out = outPath("arm64_multi_call.s");
    const std::string il = "il 0.1\n"
                           "extern @inc(i64) -> i64\n"
                           "func @chain(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %a = call @inc(%x)\n"
                           "  %b = call @inc(%a)\n"
                           "  %c = call @inc(%b)\n"
                           "  ret %c\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect three calls
    const std::string blInc = blSym("inc");
    std::size_t pos1 = asmText.find(blInc);
    EXPECT_NE(pos1, std::string::npos);
    std::size_t pos2 = asmText.find(blInc, pos1 + 1);
    EXPECT_NE(pos2, std::string::npos);
    std::size_t pos3 = asmText.find(blInc, pos2 + 1);
    EXPECT_NE(pos3, std::string::npos);
}

// Test 5: Call with >8 args where args are computed
TEST(Arm64CallConv, ManyArgsComputed)
{
    const std::string in = outPath("arm64_call_many_computed.il");
    const std::string out = outPath("arm64_call_many_computed.s");
    const std::string il = "il 0.1\n"
                           "extern @sum10(i64,i64,i64,i64,i64,i64,i64,i64,i64,i64) -> i64\n"
                           "func @f(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %v1 = add %a, 1\n"
                           "  %v2 = add %b, 2\n"
                           "  %r = call @sum10(%v1, %v2, 3, 4, 5, 6, 7, 8, 9, 10)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect stack allocation for 2 extra args (16 bytes aligned)
    EXPECT_NE(asmText.find("sub sp, sp, #16"), std::string::npos);
    EXPECT_NE(asmText.find("str x"), std::string::npos);
    EXPECT_NE(asmText.find("[sp, #0]"), std::string::npos);
    EXPECT_NE(asmText.find("[sp, #8]"), std::string::npos);
    EXPECT_NE(asmText.find(blSym("sum10")), std::string::npos);
    EXPECT_NE(asmText.find("add sp, sp, #16"), std::string::npos);
}

// Test 6: Call result used in conditional branch
TEST(Arm64CallConv, CallResultInCondition)
{
    const std::string in = outPath("arm64_call_cond.il");
    const std::string out = outPath("arm64_call_cond.s");
    const std::string il = "il 0.1\n"
                           "extern @check(i64) -> i64\n"
                           "func @f(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %c = call @check(%x)\n"
                           "  %cmp = icmp_eq %c, 0\n"
                           "  cbr %cmp, zero, nonzero\n"
                           "zero():\n"
                           "  ret 0\n"
                           "nonzero():\n"
                           "  ret 1\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find(blSym("check")), std::string::npos);
    // After the call, the result is compared (may use cmp or tst if comparing against 0)
    const bool hasCompare = asmText.find("cmp x") != std::string::npos ||
                            asmText.find("tst x") != std::string::npos;
    EXPECT_TRUE(hasCompare);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
