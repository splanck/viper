//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_cbr.cpp
// Purpose: Verify CLI lowers cbr with compare conditions to cmp + b.<cond>.
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

TEST(Arm64CLI, CBrOnCompareRR)
{
    const std::string in = outPath("arm64_cbr_rr.il");
    const std::string out = outPath("arm64_cbr_rr.s");
    const std::string il = "il 0.1\n"
                           "func @f(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %c = icmp_eq %a, %b\n"
                           "  cbr %c, t(), f()\n"
                           "t():\n"
                           "  ret 1\n"
                           "f():\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("entry:"), std::string::npos);
    EXPECT_NE(asmText.find("t:"), std::string::npos);
    EXPECT_NE(asmText.find("f:"), std::string::npos);
    EXPECT_NE(asmText.find("cmp x0, x1"), std::string::npos);
    EXPECT_NE(asmText.find("b.eq t"), std::string::npos);
}

TEST(Arm64CLI, CBrOnCompareImm)
{
    const std::string in = outPath("arm64_cbr_imm.il");
    const std::string out = outPath("arm64_cbr_imm.s");
    const std::string il = "il 0.1\n"
                           "func @g(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %c = scmp_lt %b, -7\n"
                           "  cbr %c, T(), F()\n"
                           "T():\n"
                           "  ret 1\n"
                           "F():\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect param1 moved to x0, cmp x0, #-7 and b.lt T
    EXPECT_NE(asmText.find("mov x0, x1"), std::string::npos);
    EXPECT_NE(asmText.find("cmp x0, #-7"), std::string::npos);
    EXPECT_NE(asmText.find("b.lt T"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
