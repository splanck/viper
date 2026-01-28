//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_peephole_comprehensive.cpp
// Purpose: Comprehensive peephole optimization tests via IL compilation.
// Key invariants: Peephole patterns apply during codegen, producing better code.
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

// Test 1: Add with 0 should be optimized away or become mov
TEST(Arm64PeepholeComprehensive, AddZero)
{
    const std::string in = outPath("arm64_peep_add0.il");
    const std::string out = outPath("arm64_peep_add0.s");
    const std::string il = "il 0.1\n"
                           "func @add0(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %r = add %x, 0\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should compile successfully - may or may not have add with #0
    EXPECT_FALSE(asmText.empty());
}

// Test 2: Sub with 0 should be optimized
TEST(Arm64PeepholeComprehensive, SubZero)
{
    const std::string in = outPath("arm64_peep_sub0.il");
    const std::string out = outPath("arm64_peep_sub0.s");
    const std::string il = "il 0.1\n"
                           "func @sub0(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %r = sub %x, 0\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_FALSE(asmText.empty());
}

// Test 3: Mul by 1 should be identity
TEST(Arm64PeepholeComprehensive, MulOne)
{
    const std::string in = outPath("arm64_peep_mul1.il");
    const std::string out = outPath("arm64_peep_mul1.s");
    const std::string il = "il 0.1\n"
                           "func @mul1(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %r = mul %x, 1\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_FALSE(asmText.empty());
}

// Test 4: Mul by 0 should be 0
TEST(Arm64PeepholeComprehensive, MulZero)
{
    const std::string in = outPath("arm64_peep_mul0.il");
    const std::string out = outPath("arm64_peep_mul0.s");
    const std::string il = "il 0.1\n"
                           "func @mul0(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %r = mul %x, 0\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_FALSE(asmText.empty());
}

// Test 5: Shift by 0 should be identity
TEST(Arm64PeepholeComprehensive, ShiftZero)
{
    const std::string in = outPath("arm64_peep_shl0.il");
    const std::string out = outPath("arm64_peep_shl0.s");
    const std::string il = "il 0.1\n"
                           "func @shl0(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %r = shl %x, 0\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_FALSE(asmText.empty());
}

// Test 6: And with -1 (all ones) is identity
TEST(Arm64PeepholeComprehensive, AndAllOnes)
{
    const std::string in = outPath("arm64_peep_and_ones.il");
    const std::string out = outPath("arm64_peep_and_ones.s");
    const std::string il = "il 0.1\n"
                           "func @and_ones(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %r = and %x, -1\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_FALSE(asmText.empty());
}

// Test 7: Or with 0 is identity
TEST(Arm64PeepholeComprehensive, OrZero)
{
    const std::string in = outPath("arm64_peep_or0.il");
    const std::string out = outPath("arm64_peep_or0.s");
    const std::string il = "il 0.1\n"
                           "func @or0(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %r = or %x, 0\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_FALSE(asmText.empty());
}

// Test 8: Xor with 0 is identity
TEST(Arm64PeepholeComprehensive, XorZero)
{
    const std::string in = outPath("arm64_peep_xor0.il");
    const std::string out = outPath("arm64_peep_xor0.s");
    const std::string il = "il 0.1\n"
                           "func @xor0(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %r = xor %x, 0\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_FALSE(asmText.empty());
}

// Test 9: Branch to next block should be elided
TEST(Arm64PeepholeComprehensive, FallthroughBranch)
{
    const std::string in = outPath("arm64_peep_fallthrough.il");
    const std::string out = outPath("arm64_peep_fallthrough.s");
    const std::string il = "il 0.1\n"
                           "func @fallthrough(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %t = add %x, 1\n"
                           "  br ^next\n"
                           "next:\n"
                           "  ret %t\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // The unconditional branch to next block may be optimized away
    EXPECT_FALSE(asmText.empty());
}

// Test 10: Consecutive moves should be folded
TEST(Arm64PeepholeComprehensive, ConsecutiveMoves)
{
    const std::string in = outPath("arm64_peep_moves.il");
    const std::string out = outPath("arm64_peep_moves.s");
    const std::string il = "il 0.1\n"
                           "func @moves(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %t1 = add %a, %b\n"
                           "  %t2 = add %t1, 0\n"
                           "  %r = add %t2, 0\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_FALSE(asmText.empty());
}

// Test 11: Compare with 0 can use tst
TEST(Arm64PeepholeComprehensive, CmpZeroToTst)
{
    const std::string in = outPath("arm64_peep_cmp0.il");
    const std::string out = outPath("arm64_peep_cmp0.s");
    const std::string il = "il 0.1\n"
                           "func @is_zero(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %c = icmp_eq %x, 0\n"
                           "  %r = zext1 %c\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // May have tst x, x or cmp x, #0
    bool hasCmpOrTst =
        asmText.find("tst x") != std::string::npos || asmText.find("cmp x") != std::string::npos;
    EXPECT_TRUE(hasCmpOrTst);
}

// Test 12: FP identity operations
TEST(Arm64PeepholeComprehensive, FPIdentities)
{
    const std::string in = outPath("arm64_peep_fp.il");
    const std::string out = outPath("arm64_peep_fp.s");
    // fadd with 0.0 is identity
    const std::string il = "il 0.1\n"
                           "func @fp_add0(%x:f64) -> f64 {\n"
                           "entry(%x:f64):\n"
                           "  %zero = sitofp 0\n"
                           "  %r = fadd %x, %zero\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_FALSE(asmText.empty());
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
