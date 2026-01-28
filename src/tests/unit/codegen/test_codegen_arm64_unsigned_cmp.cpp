//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_unsigned_cmp.cpp
// Purpose: Verify unsigned comparison operations on AArch64.
// Key invariants: ucmp uses unsigned condition codes (hi, hs, lo, ls).
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

// Test all unsigned comparison operators
TEST(Arm64UnsignedCmp, AllComparisons)
{
    struct Case
    {
        const char *op;
        const char *expectedCond; // May have alternatives
    } cases[] = {
        {"ucmp_lt", "lo"}, // unsigned less than -> lo (lower)
        {"ucmp_le", "ls"}, // unsigned less or equal -> ls (lower or same)
        {"ucmp_gt", "hi"}, // unsigned greater than -> hi (higher)
        {"ucmp_ge", "hs"}, // unsigned greater or equal -> hs (higher or same) or cs
    };

    for (const auto &c : cases)
    {
        std::string in = std::string("arm64_") + c.op + ".il";
        std::string out = std::string("arm64_") + c.op + ".s";
        std::string il = std::string("il 0.1\n"
                                     "func @cmp(%a:i64, %b:i64) -> i64 {\n"
                                     "entry(%a:i64, %b:i64):\n"
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
        // Expect cmp instruction
        EXPECT_NE(asmText.find("cmp x"), std::string::npos);
        // Expect cset with unsigned condition
        EXPECT_NE(asmText.find("cset x"), std::string::npos);
        // Verify it uses the expected unsigned condition code
        EXPECT_NE(asmText.find(c.expectedCond), std::string::npos);
    }
}

// Test: ucmp in conditional branch
TEST(Arm64UnsignedCmp, BranchOnUcmp)
{
    const std::string in = outPath("arm64_ucmp_branch.il");
    const std::string out = outPath("arm64_ucmp_branch.s");
    const std::string il = "il 0.1\n"
                           "func @umax(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %c = ucmp_gt %a, %b\n"
                           "  cbr %c, ^ta, ^tb\n"
                           "ta:\n"
                           "  ret %a\n"
                           "tb:\n"
                           "  ret %b\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have conditional branch with unsigned condition (b.hi)
    EXPECT_NE(asmText.find("b.hi"), std::string::npos);
}

// Test: ucmp vs scmp difference (same values, different results for negatives)
TEST(Arm64UnsignedCmp, UcmpVsScmp)
{
    // Test with unsigned comparison
    {
        const std::string in = outPath("arm64_ucmp_neg.il");
        const std::string out = outPath("arm64_ucmp_neg.s");
        const std::string il = "il 0.1\n"
                               "func @ucmp_lt(%a:i64, %b:i64) -> i64 {\n"
                               "entry(%a:i64, %b:i64):\n"
                               "  %c = ucmp_lt %a, %b\n"
                               "  %r = zext1 %c\n"
                               "  ret %r\n"
                               "}\n";
        writeFile(in, il);
        const char *argv[] = {in.c_str(), "-S", out.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(out);
        // Should use unsigned condition lo (lower)
        EXPECT_NE(asmText.find("lo"), std::string::npos);
    }

    // Test with signed comparison
    {
        const std::string in = outPath("arm64_scmp_neg.il");
        const std::string out = outPath("arm64_scmp_neg.s");
        const std::string il = "il 0.1\n"
                               "func @scmp_lt(%a:i64, %b:i64) -> i64 {\n"
                               "entry(%a:i64, %b:i64):\n"
                               "  %c = scmp_lt %a, %b\n"
                               "  %r = zext1 %c\n"
                               "  ret %r\n"
                               "}\n";
        writeFile(in, il);
        const char *argv[] = {in.c_str(), "-S", out.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(out);
        // Should use signed condition lt (less than)
        EXPECT_NE(asmText.find("lt"), std::string::npos);
    }
}

// Test: ucmp with immediate
TEST(Arm64UnsignedCmp, UcmpImmediate)
{
    const std::string in = outPath("arm64_ucmp_imm.il");
    const std::string out = outPath("arm64_ucmp_imm.s");
    const std::string il = "il 0.1\n"
                           "func @ucmp_lt_imm(%a:i64) -> i64 {\n"
                           "entry(%a:i64):\n"
                           "  %c = ucmp_lt %a, 100\n"
                           "  %r = zext1 %c\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have cmp with immediate
    EXPECT_NE(asmText.find("cmp x"), std::string::npos);
}

// Test: Chained unsigned comparisons (bounds check pattern)
TEST(Arm64UnsignedCmp, BoundsCheck)
{
    const std::string in = outPath("arm64_ucmp_bounds.il");
    const std::string out = outPath("arm64_ucmp_bounds.s");
    // Check if index is in bounds: 0 <= idx < len
    // For unsigned, this is just idx < len (negative treated as large positive)
    const std::string il = "il 0.1\n"
                           "func @in_bounds(%idx:i64, %len:i64) -> i64 {\n"
                           "entry(%idx:i64, %len:i64):\n"
                           "  %c = ucmp_lt %idx, %len\n"
                           "  %r = zext1 %c\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should use unsigned condition
    EXPECT_NE(asmText.find("lo"), std::string::npos);
}

// Test: ucmp_eq and ucmp_ne (same as icmp_eq/ne for these)
TEST(Arm64UnsignedCmp, EqualityComparisons)
{
    struct Case
    {
        const char *op;
        const char *cond;
    } cases[] = {
        {"icmp_eq", "eq"},
        {"icmp_ne", "ne"},
    };

    for (const auto &c : cases)
    {
        std::string in = std::string("arm64_") + c.op + "_u.il";
        std::string out = std::string("arm64_") + c.op + "_u.s";
        std::string il = std::string("il 0.1\n"
                                     "func @cmp(%a:i64, %b:i64) -> i64 {\n"
                                     "entry(%a:i64, %b:i64):\n"
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
        EXPECT_NE(asmText.find(c.cond), std::string::npos);
    }
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
