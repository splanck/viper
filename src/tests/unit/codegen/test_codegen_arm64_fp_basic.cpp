//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_fp_basic.cpp
// Purpose: Minimal tests for AArch64 FP lowering: ops and calls using v0..v7
// Key invariants: To be documented.
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

TEST(Arm64FP, FAddTwoParams)
{
    const std::string in = outPath("arm64_fp_add2.il");
    const std::string out = outPath("arm64_fp_add2.s");
    const std::string il = "il 0.1\n"
                           "func @f(%a:f64, %b:f64) -> f64 {\n"
                           "entry(%a:f64, %b:f64):\n"
                           "  %t0 = fadd %a, %b\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect fadd using first two FP argument registers.
    EXPECT_NE(asmText.find("fadd d0, d0, d1"), std::string::npos);
}

TEST(Arm64FP, CallTwoDoubles)
{
    const std::string in = outPath("arm64_fp_call.il");
    const std::string out = outPath("arm64_fp_call.s");
    const std::string il = "il 0.1\n"
                           "extern @h(f64, f64) -> f64\n"
                           "func @f(%a:f64, %b:f64) -> f64 {\n"
                           "entry(%a:f64, %b:f64):\n"
                           "  %t0 = call @h(%a, %b)\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect marshalling to d0,d1 and a call
    EXPECT_NE(asmText.find("fmov d0"), std::string::npos);
    EXPECT_NE(asmText.find("fmov d1"), std::string::npos);
    EXPECT_NE(asmText.find("bl h"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
