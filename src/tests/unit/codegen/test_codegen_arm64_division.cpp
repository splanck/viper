//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_division.cpp
// Purpose: Verify signed and unsigned division (sdiv/udiv) lowering on AArch64.
// Key invariants: Emits sdiv/udiv instructions with divide-by-zero checks.
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

// Test 1: Simple signed division
TEST(Arm64Division, SDivSimple)
{
    const std::string in = outPath("arm64_div_sdiv.il");
    const std::string out = outPath("arm64_div_sdiv.s");
    const std::string il = "il 0.1\n"
                           "func @div(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %r = sdiv %a, %b\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect sdiv instruction
    EXPECT_NE(asmText.find("sdiv x"), std::string::npos);
}

// Test 2: Simple unsigned division
TEST(Arm64Division, UDivSimple)
{
    const std::string in = outPath("arm64_div_udiv.il");
    const std::string out = outPath("arm64_div_udiv.s");
    const std::string il = "il 0.1\n"
                           "func @udiv(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %r = udiv %a, %b\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect udiv instruction
    EXPECT_NE(asmText.find("udiv x"), std::string::npos);
}

// Test 3: Signed division with divide-by-zero check
TEST(Arm64Division, SDivChk0)
{
    const std::string in = outPath("arm64_div_sdiv_chk0.il");
    const std::string out = outPath("arm64_div_sdiv_chk0.s");
    const std::string il = "il 0.1\n"
                           "func @div_chk(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %r = sdiv.chk0 %a, %b\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect sdiv instruction
    EXPECT_NE(asmText.find("sdiv x"), std::string::npos);
    // Expect zero-check: either cbz, cmp #0 + b.eq, or tst + b.eq
    bool hasZeroCheck =
        asmText.find("cbz x") != std::string::npos ||
        (asmText.find("cmp x") != std::string::npos && asmText.find("b.eq") != std::string::npos) ||
        (asmText.find("tst x") != std::string::npos && asmText.find("b.eq") != std::string::npos);
    EXPECT_TRUE(hasZeroCheck);
}

// Test 4: Unsigned division with divide-by-zero check
TEST(Arm64Division, UDivChk0)
{
    const std::string in = outPath("arm64_div_udiv_chk0.il");
    const std::string out = outPath("arm64_div_udiv_chk0.s");
    const std::string il = "il 0.1\n"
                           "func @udiv_chk(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %r = udiv.chk0 %a, %b\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect udiv instruction
    EXPECT_NE(asmText.find("udiv x"), std::string::npos);
    // Expect zero-check: either cbz, cmp #0 + b.eq, or tst + b.eq
    bool hasZeroCheck =
        asmText.find("cbz x") != std::string::npos ||
        (asmText.find("cmp x") != std::string::npos && asmText.find("b.eq") != std::string::npos) ||
        (asmText.find("tst x") != std::string::npos && asmText.find("b.eq") != std::string::npos);
    EXPECT_TRUE(hasZeroCheck);
}

// Test 5: Signed remainder (srem = a - (a/b)*b using msub)
TEST(Arm64Division, SRemSimple)
{
    const std::string in = outPath("arm64_div_srem.il");
    const std::string out = outPath("arm64_div_srem.s");
    const std::string il = "il 0.1\n"
                           "func @rem(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %r = srem %a, %b\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect sdiv for the division part
    EXPECT_NE(asmText.find("sdiv x"), std::string::npos);
    // Expect msub for the remainder calculation (dst = sub - mul1*mul2)
    EXPECT_NE(asmText.find("msub x"), std::string::npos);
}

// Test 6: Unsigned remainder
TEST(Arm64Division, URemSimple)
{
    const std::string in = outPath("arm64_div_urem.il");
    const std::string out = outPath("arm64_div_urem.s");
    const std::string il = "il 0.1\n"
                           "func @urem(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %r = urem %a, %b\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect udiv for the division part
    EXPECT_NE(asmText.find("udiv x"), std::string::npos);
    // Expect msub for the remainder calculation
    EXPECT_NE(asmText.find("msub x"), std::string::npos);
}

// Test 7: Division by constant (could potentially be optimized)
TEST(Arm64Division, DivByConstant)
{
    const std::string in = outPath("arm64_div_const.il");
    const std::string out = outPath("arm64_div_const.s");
    const std::string il = "il 0.1\n"
                           "func @divby4(%a:i64) -> i64 {\n"
                           "entry(%a:i64):\n"
                           "  %r = sdiv %a, 4\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Could be either sdiv or optimized to shift
    // At minimum, the code should compile
    EXPECT_FALSE(asmText.empty());
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
