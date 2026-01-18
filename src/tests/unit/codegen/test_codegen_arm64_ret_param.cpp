//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_ret_param.cpp
// Purpose: Verify returning parameters lowers to correct moves/no-op.
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

TEST(Arm64CLI, RetParam)
{
    // Return param0: no mov expected (x0 already contains arg0)
    {
        const std::string in = "arm64_ret_p0.il";
        const std::string out = "arm64_ret_p0.s";
        const std::string il = "il 0.1\n"
                               "func @id0(%a:i64, %b:i64) -> i64 {\n"
                               "entry(%a:i64, %b:i64):\n"
                               "  ret %a\n"
                               "}\n";
        const std::string inP = outPath(in);
        const std::string outP = outPath(out);
        writeFile(inP, il);
        const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(outP);
        EXPECT_EQ(asmText.find("mov x0, x1"), std::string::npos);
    }

    // Return param1: expect mov x0, x1
    {
        const std::string in = "arm64_ret_p1.il";
        const std::string out = "arm64_ret_p1.s";
        const std::string il = "il 0.1\n"
                               "func @id1(%a:i64, %b:i64) -> i64 {\n"
                               "entry(%a:i64, %b:i64):\n"
                               "  ret %b\n"
                               "}\n";
        const std::string inP = outPath(in);
        const std::string outP = outPath(out);
        writeFile(inP, il);
        const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(outP);
        EXPECT_NE(asmText.find("mov x0, x1"), std::string::npos);
    }
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
