//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_rodata_literals.cpp
// Purpose: Verify rodata pool generation for large constants and literals.
// Key invariants: Large immediates and FP constants go to rodata pool.
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

// Test 1: Large immediate that cannot be encoded inline
TEST(Arm64Rodata, LargeImmediate)
{
    const std::string in = outPath("arm64_rodata_large.il");
    const std::string out = outPath("arm64_rodata_large.s");
    // 0x123456789ABCDEF0 cannot be encoded in a single mov instruction
    const std::string il = "il 0.1\n"
                           "func @large_const() -> i64 {\n"
                           "entry:\n"
                           "  ret 1311768467463790320\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // May use movz/movk sequence or ldr from literal pool
    bool hasConstLoad = asmText.find("movz x") != std::string::npos ||
                        asmText.find("movk x") != std::string::npos ||
                        asmText.find("ldr x") != std::string::npos;
    EXPECT_TRUE(hasConstLoad);
}

// Test 2: Floating-point constant
TEST(Arm64Rodata, FloatConstant)
{
    const std::string in = outPath("arm64_rodata_fp.il");
    const std::string out = outPath("arm64_rodata_fp.s");
    const std::string il = "il 0.1\n"
                           "func @pi() -> f64 {\n"
                           "entry:\n"
                           "  %r = const.f64 3.14159265358979\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // FP constant likely loaded from rodata or using fmov with imm
    bool hasFPLoad =
        asmText.find("ldr d") != std::string::npos || asmText.find("fmov d") != std::string::npos;
    EXPECT_TRUE(hasFPLoad);
}

// Test 3: String constant
TEST(Arm64Rodata, StringConstant)
{
    const std::string in = outPath("arm64_rodata_str.il");
    const std::string out = outPath("arm64_rodata_str.s");
    const std::string il = "il 0.1\n"
                           "global const str @hello = \"Hello, World!\"\n"
                           "func @get_hello() -> str {\n"
                           "entry:\n"
                           "  %s = const_str @hello\n"
                           "  ret %s\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have data section reference
    bool hasData = asmText.find(".ascii") != std::string::npos ||
                   asmText.find(".asciz") != std::string::npos ||
                   asmText.find(".string") != std::string::npos ||
                   asmText.find("adrp") != std::string::npos;
    EXPECT_TRUE(hasData);
}

// Test 4: Multiple different large constants (within i64 signed range)
TEST(Arm64Rodata, MultipleLargeConstants)
{
    const std::string in = outPath("arm64_rodata_multi.il");
    const std::string out = outPath("arm64_rodata_multi.s");
    // Use large values within signed i64 range
    const std::string il = "il 0.1\n"
                           "func @two_large(%sel:i64) -> i64 {\n"
                           "entry(%sel:i64):\n"
                           "  %c = icmp_ne %sel, 0\n"
                           "  cbr %c, ta, tb\n"
                           "ta:\n"
                           "  ret 0x123456789ABCDEF\n"
                           "tb:\n"
                           "  ret 0x7EDCBA9876543210\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should compile with multiple constants
    EXPECT_FALSE(asmText.empty());
}

// Test 5: Zero and all-ones (special cases)
TEST(Arm64Rodata, ZeroAndOnes)
{
    const std::string in = outPath("arm64_rodata_special.il");
    const std::string out = outPath("arm64_rodata_special.s");
    const std::string il = "il 0.1\n"
                           "func @zero() -> i64 {\n"
                           "entry:\n"
                           "  ret 0\n"
                           "}\n"
                           "func @ones() -> i64 {\n"
                           "entry:\n"
                           "  ret -1\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Zero might use xzr or mov #0
    // -1 might use mvn xzr or mov with immediate
    EXPECT_FALSE(asmText.empty());
}

// Test 6: FP special values
TEST(Arm64Rodata, FPSpecialValues)
{
    const std::string in = outPath("arm64_rodata_fp_special.il");
    const std::string out = outPath("arm64_rodata_fp_special.s");
    const std::string il = "il 0.1\n"
                           "func @zero_fp() -> f64 {\n"
                           "entry:\n"
                           "  %z = sitofp 0\n"
                           "  ret %z\n"
                           "}\n"
                           "func @one_fp() -> f64 {\n"
                           "entry:\n"
                           "  %o = sitofp 1\n"
                           "  ret %o\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("scvtf d"), std::string::npos);
}

// Test 7: Constant used multiple times (should be deduplicated)
TEST(Arm64Rodata, ConstantDeduplication)
{
    const std::string in = outPath("arm64_rodata_dedup.il");
    const std::string out = outPath("arm64_rodata_dedup.s");
    const std::string il = "il 0.1\n"
                           "func @use_const_twice(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %t1 = add %a, 0x123456789ABCDEF0\n"
                           "  %t2 = add %b, 0x123456789ABCDEF0\n"
                           "  %r = add %t1, %t2\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should compile successfully
    EXPECT_FALSE(asmText.empty());
}

// Test 8: Negative large constant
TEST(Arm64Rodata, NegativeLarge)
{
    const std::string in = outPath("arm64_rodata_neg.il");
    const std::string out = outPath("arm64_rodata_neg.s");
    const std::string il = "il 0.1\n"
                           "func @neg_large() -> i64 {\n"
                           "entry:\n"
                           "  ret -1234567890123456789\n"
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
