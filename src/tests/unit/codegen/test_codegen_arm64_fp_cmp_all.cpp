//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_fp_cmp_all.cpp
// Purpose: Verify all floating-point comparison operations on AArch64.
// Key invariants: All fcmp variants emit fcmp + appropriate cset.
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

// Test all FP comparison operators
TEST(Arm64FPCmpAll, AllComparisons)
{
    struct Case
    {
        const char *op;
        const char *desc;
    } cases[] = {
        {"fcmp_eq", "equal"},
        {"fcmp_ne", "not equal"},
        {"fcmp_lt", "less than"},
        {"fcmp_le", "less or equal"},
        {"fcmp_gt", "greater than"},
        {"fcmp_ge", "greater or equal"},
    };

    for (const auto &c : cases)
    {
        std::string in = std::string("arm64_fp_") + c.op + ".il";
        std::string out = std::string("arm64_fp_") + c.op + ".s";
        std::string il = std::string("il 0.1\n"
                                     "func @cmp(%a:f64, %b:f64) -> i64 {\n"
                                     "entry(%a:f64, %b:f64):\n"
                                     "  %c = ") +
                         c.op +
                         " %a, %b\n"
                         "  %r = zext1 %c\n"
                         "  ret %r\n"
                         "}\n";
        const std::string inP = outPath(in);
        const std::string outP = outPath(out);
        writeFile(inP, il);
        const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(outP);
        // Expect fcmp instruction
        EXPECT_NE(asmText.find("fcmp d"), std::string::npos);
        // Expect cset for the result
        EXPECT_NE(asmText.find("cset x"), std::string::npos);
    }
}

// Test: fcmp_ord (ordered - neither is NaN)
TEST(Arm64FPCmpAll, Ordered)
{
    const std::string in = outPath("arm64_fp_fcmp_ord.il");
    const std::string out = outPath("arm64_fp_fcmp_ord.s");
    const std::string il = "il 0.1\n"
                           "func @ord(%a:f64, %b:f64) -> i64 {\n"
                           "entry(%a:f64, %b:f64):\n"
                           "  %c = fcmp_ord %a, %b\n"
                           "  %r = zext1 %c\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have fcmp
    EXPECT_NE(asmText.find("fcmp d"), std::string::npos);
}

// Test: fcmp_uno (unordered - at least one is NaN)
TEST(Arm64FPCmpAll, Unordered)
{
    const std::string in = outPath("arm64_fp_fcmp_uno.il");
    const std::string out = outPath("arm64_fp_fcmp_uno.s");
    const std::string il = "il 0.1\n"
                           "func @uno(%a:f64, %b:f64) -> i64 {\n"
                           "entry(%a:f64, %b:f64):\n"
                           "  %c = fcmp_uno %a, %b\n"
                           "  %r = zext1 %c\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have fcmp
    EXPECT_NE(asmText.find("fcmp d"), std::string::npos);
}

// Test: FP comparison feeding conditional branch
TEST(Arm64FPCmpAll, CmpBranch)
{
    const std::string in = outPath("arm64_fp_cmp_branch.il");
    const std::string out = outPath("arm64_fp_cmp_branch.s");
    const std::string il = "il 0.1\n"
                           "func @max(%a:f64, %b:f64) -> f64 {\n"
                           "entry(%a:f64, %b:f64):\n"
                           "  %c = fcmp_gt %a, %b\n"
                           "  cbr %c, ta, tb\n"
                           "ta:\n"
                           "  ret %a\n"
                           "tb:\n"
                           "  ret %b\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have fcmp
    EXPECT_NE(asmText.find("fcmp d"), std::string::npos);
    // Should have conditional branch
    EXPECT_NE(asmText.find("b."), std::string::npos);
}

// Test: Chained FP comparisons
TEST(Arm64FPCmpAll, ChainedComparisons)
{
    const std::string in = outPath("arm64_fp_chain_cmp.il");
    const std::string out = outPath("arm64_fp_chain_cmp.s");
    // Check if x is in range [lo, hi)
    const std::string il = "il 0.1\n"
                           "func @inrange(%x:f64, %lo:f64, %hi:f64) -> i64 {\n"
                           "entry(%x:f64, %lo:f64, %hi:f64):\n"
                           "  %c1 = fcmp_ge %x, %lo\n"
                           "  %c2 = fcmp_lt %x, %hi\n"
                           "  %i1 = zext1 %c1\n"
                           "  %i2 = zext1 %c2\n"
                           "  %r = and %i1, %i2\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have multiple fcmp
    std::size_t fcmpCount = 0;
    std::size_t pos = 0;
    while ((pos = asmText.find("fcmp d", pos)) != std::string::npos)
    {
        ++fcmpCount;
        pos += 6;
    }
    EXPECT_TRUE(fcmpCount >= 2U);
}

// Test: FP comparison with constant
TEST(Arm64FPCmpAll, CmpWithZero)
{
    const std::string in = outPath("arm64_fp_cmp_zero.il");
    const std::string out = outPath("arm64_fp_cmp_zero.s");
    const std::string il = "il 0.1\n"
                           "func @is_positive(%x:f64) -> i64 {\n"
                           "entry(%x:f64):\n"
                           "  %zero = sitofp 0\n"
                           "  %c = fcmp_gt %x, %zero\n"
                           "  %r = zext1 %c\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("fcmp d"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
