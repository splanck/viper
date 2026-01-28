//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_sub_mul.cpp
// Purpose: Verify subtraction and multiplication lowering on AArch64.
// Key invariants: Emits sub and mul instructions correctly.
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

// Test 1: Simple subtraction of two parameters
TEST(Arm64SubMul, SubSimple)
{
    const std::string in = outPath("arm64_sub_simple.il");
    const std::string out = outPath("arm64_sub_simple.s");
    const std::string il = "il 0.1\n"
                           "func @sub(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %r = sub %a, %b\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect sub instruction
    EXPECT_NE(asmText.find("sub x"), std::string::npos);
}

// Test 2: Subtraction with immediate
TEST(Arm64SubMul, SubImmediate)
{
    const std::string in = outPath("arm64_sub_imm.il");
    const std::string out = outPath("arm64_sub_imm.s");
    const std::string il = "il 0.1\n"
                           "func @sub5(%a:i64) -> i64 {\n"
                           "entry(%a:i64):\n"
                           "  %r = sub %a, 5\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect sub with immediate (sub xN, xM, #5)
    EXPECT_NE(asmText.find("sub x"), std::string::npos);
}

// Test 3: Simple multiplication of two parameters
TEST(Arm64SubMul, MulSimple)
{
    const std::string in = outPath("arm64_mul_simple.il");
    const std::string out = outPath("arm64_mul_simple.s");
    const std::string il = "il 0.1\n"
                           "func @mul(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %r = mul %a, %b\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect mul instruction
    EXPECT_NE(asmText.find("mul x"), std::string::npos);
}

// Test 4: Multiplication by power of 2 (could be optimized to shift)
TEST(Arm64SubMul, MulPowerOf2)
{
    const std::string in = outPath("arm64_mul_pow2.il");
    const std::string out = outPath("arm64_mul_pow2.s");
    const std::string il = "il 0.1\n"
                           "func @mul8(%a:i64) -> i64 {\n"
                           "entry(%a:i64):\n"
                           "  %r = mul %a, 8\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Could be mul or optimized to lsl (shift left by 3)
    bool hasMulOrShift =
        asmText.find("mul x") != std::string::npos || asmText.find("lsl x") != std::string::npos;
    EXPECT_TRUE(hasMulOrShift);
}

// Test 5: Multiply-accumulate pattern (a + b*c)
TEST(Arm64SubMul, MulAccumulate)
{
    const std::string in = outPath("arm64_mul_acc.il");
    const std::string out = outPath("arm64_mul_acc.s");
    const std::string il = "il 0.1\n"
                           "func @madd(%a:i64, %b:i64, %c:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64, %c:i64):\n"
                           "  %t = mul %b, %c\n"
                           "  %r = add %a, %t\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Could be madd instruction or separate mul+add
    // At minimum should have the arithmetic
    bool hasArith =
        asmText.find("mul x") != std::string::npos || asmText.find("madd x") != std::string::npos;
    EXPECT_TRUE(hasArith);
}

// Test 6: Chained subtraction
TEST(Arm64SubMul, SubChained)
{
    const std::string in = outPath("arm64_sub_chain.il");
    const std::string out = outPath("arm64_sub_chain.s");
    const std::string il = "il 0.1\n"
                           "func @sub_chain(%a:i64, %b:i64, %c:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64, %c:i64):\n"
                           "  %t = sub %a, %b\n"
                           "  %r = sub %t, %c\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Count sub instructions
    std::size_t subCount = 0;
    std::size_t pos = 0;
    while ((pos = asmText.find("sub x", pos)) != std::string::npos)
    {
        ++subCount;
        pos += 5;
    }
    EXPECT_TRUE(subCount >= 2U);
}

// Test 7: Mixed arithmetic (a*b - c)
TEST(Arm64SubMul, MixedArith)
{
    const std::string in = outPath("arm64_mixed_arith.il");
    const std::string out = outPath("arm64_mixed_arith.s");
    const std::string il = "il 0.1\n"
                           "func @expr(%a:i64, %b:i64, %c:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64, %c:i64):\n"
                           "  %t = mul %a, %b\n"
                           "  %r = sub %t, %c\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have both mul and sub
    EXPECT_NE(asmText.find("mul x"), std::string::npos);
    EXPECT_NE(asmText.find("sub x"), std::string::npos);
}

// Test 8: Negation via subtraction from zero
TEST(Arm64SubMul, Negate)
{
    const std::string in = outPath("arm64_negate.il");
    const std::string out = outPath("arm64_negate.s");
    const std::string il = "il 0.1\n"
                           "func @neg(%a:i64) -> i64 {\n"
                           "entry(%a:i64):\n"
                           "  %r = sub 0, %a\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Could be neg or sub from xzr
    bool hasNegate =
        asmText.find("neg x") != std::string::npos || asmText.find("sub x") != std::string::npos;
    EXPECT_TRUE(hasNegate);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
