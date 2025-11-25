//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_fp.cpp
// Purpose: Verify floating-point support in AArch64 backend.
//
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "tools/ilc/cmd_codegen_arm64.hpp"

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

// Test 1: FP addition - f(x: f64) -> f64 returns x + 1.0
TEST(Arm64FP, FAddSimple)
{
    const std::string in = outPath("arm64_fp_fadd.il");
    const std::string out = outPath("arm64_fp_fadd.s");
    // f64 parameters use v0, returns in v0
    // Note: We need to materialize 1.0 which requires sitofp from an integer
    const std::string il = "il 0.1\n"
                           "func @fadd1(%x:f64) -> f64 {\n"
                           "entry(%x:f64):\n"
                           "  %one = sitofp 1\n"
                           "  %r = fadd %x, %one\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect fadd with d-registers
    EXPECT_NE(asmText.find("fadd d"), std::string::npos);
    // Expect scvtf for sitofp
    EXPECT_NE(asmText.find("scvtf d"), std::string::npos);
}

// Test 2: FP subtraction
TEST(Arm64FP, FSubSimple)
{
    const std::string in = outPath("arm64_fp_fsub.il");
    const std::string out = outPath("arm64_fp_fsub.s");
    const std::string il = "il 0.1\n"
                           "func @fsub1(%x:f64, %y:f64) -> f64 {\n"
                           "entry(%x:f64, %y:f64):\n"
                           "  %r = fsub %x, %y\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("fsub d"), std::string::npos);
}

// Test 3: FP multiplication
TEST(Arm64FP, FMulSimple)
{
    const std::string in = outPath("arm64_fp_fmul.il");
    const std::string out = outPath("arm64_fp_fmul.s");
    const std::string il = "il 0.1\n"
                           "func @fmul1(%x:f64, %y:f64) -> f64 {\n"
                           "entry(%x:f64, %y:f64):\n"
                           "  %r = fmul %x, %y\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("fmul d"), std::string::npos);
}

// Test 4: FP division
TEST(Arm64FP, FDivSimple)
{
    const std::string in = outPath("arm64_fp_fdiv.il");
    const std::string out = outPath("arm64_fp_fdiv.s");
    const std::string il = "il 0.1\n"
                           "func @fdiv1(%x:f64, %y:f64) -> f64 {\n"
                           "entry(%x:f64, %y:f64):\n"
                           "  %r = fdiv %x, %y\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("fdiv d"), std::string::npos);
}

// Test 5: Integer to FP conversion (sitofp)
TEST(Arm64FP, SitofpConversion)
{
    const std::string in = outPath("arm64_fp_sitofp.il");
    const std::string out = outPath("arm64_fp_sitofp.s");
    const std::string il = "il 0.1\n"
                           "func @itof(%x:i64) -> f64 {\n"
                           "entry(%x:i64):\n"
                           "  %r = sitofp %x\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect scvtf dN, xM
    EXPECT_NE(asmText.find("scvtf d"), std::string::npos);
    // Return value should go through v0
    EXPECT_NE(asmText.find("fmov d0"), std::string::npos);
}

// Test 6: FP to integer conversion (fptosi)
TEST(Arm64FP, FptosiConversion)
{
    const std::string in = outPath("arm64_fp_fptosi.il");
    const std::string out = outPath("arm64_fp_fptosi.s");
    const std::string il = "il 0.1\n"
                           "func @ftoi(%x:f64) -> i64 {\n"
                           "entry(%x:f64):\n"
                           "  %r = fptosi %x\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect fcvtzs xN, dM
    EXPECT_NE(asmText.find("fcvtzs x"), std::string::npos);
}

// Test 7: FP comparison (fcmp_lt)
TEST(Arm64FP, FCmpLT)
{
    const std::string in = outPath("arm64_fp_fcmp_lt.il");
    const std::string out = outPath("arm64_fp_fcmp_lt.s");
    const std::string il = "il 0.1\n"
                           "func @cmplt(%x:f64, %y:f64) -> i64 {\n"
                           "entry(%x:f64, %y:f64):\n"
                           "  %c = fcmp_lt %x, %y\n"
                           "  %r = zext1 %c\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect fcmp dN, dM
    EXPECT_NE(asmText.find("fcmp d"), std::string::npos);
    // Expect cset for the result
    EXPECT_NE(asmText.find("cset x"), std::string::npos);
}

// Test 8: Call an extern FP function and return its result
TEST(Arm64FP, CallFPExtern)
{
    const std::string in = outPath("arm64_fp_call.il");
    const std::string out = outPath("arm64_fp_call.s");
    const std::string il = "il 0.1\n"
                           "extern @rt_add_double(f64, f64) -> f64\n"
                           "func @caller(%a:f64, %b:f64) -> f64 {\n"
                           "entry(%a:f64, %b:f64):\n"
                           "  %r = call @rt_add_double(%a, %b)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect bl rt_add_double
    EXPECT_NE(asmText.find("bl rt_add_double"), std::string::npos);
    // Args should be marshalled to v0, v1 for FP
    // Result comes back in v0
}

// Test 9: Mixed integer and FP call
TEST(Arm64FP, MixedCall)
{
    const std::string in = outPath("arm64_fp_mixed.il");
    const std::string out = outPath("arm64_fp_mixed.s");
    const std::string il = "il 0.1\n"
                           "extern @mixed(i64, f64) -> f64\n"
                           "func @caller(%n:i64, %x:f64) -> f64 {\n"
                           "entry(%n:i64, %x:f64):\n"
                           "  %r = call @mixed(%n, %x)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("bl mixed"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
