//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_switch.cpp
// Purpose: Verify AArch64 lowering for IL switch.i32 into cmp + b.eq chains.
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

TEST(Arm64CLI, SwitchSmall)
{
    const std::string in = outPath("arm64_switch_small.il");
    const std::string out = outPath("arm64_switch_small.s");
    const std::string il = "il 0.1\n"
                           "func @f(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  switch.i32 %x, ^Ld, 1 -> ^L1, 2 -> ^L2\n"
                           "L1():\n"
                           "  ret 10\n"
                           "L2():\n"
                           "  ret 20\n"
                           "Ld():\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect cmp <reg>, #1; b.eq L1 and cmp <reg>, #2; b.eq L2 then branch to default
    EXPECT_NE(asmText.find("cmp"), std::string::npos);
    EXPECT_NE(asmText.find("#1"), std::string::npos);
    EXPECT_NE(asmText.find("b.eq L1"), std::string::npos);
    EXPECT_NE(asmText.find("#2"), std::string::npos);
    EXPECT_NE(asmText.find("b.eq L2"), std::string::npos);
    EXPECT_NE(asmText.find("b Ld"), std::string::npos);
}

TEST(Arm64CLI, SwitchMany)
{
    const std::string in = outPath("arm64_switch_many.il");
    const std::string out = outPath("arm64_switch_many.s");
    std::ostringstream il;
    il << "il 0.1\n";
    il << "func @g(%x:i64) -> i64 {\n";
    il << "entry(%x:i64):\n";
    il << "  switch.i32 %x, ^Ld";
    for (int i = 0; i < 8; ++i)
        il << ", " << i << " -> ^L" << i;
    il << "\n";
    for (int i = 0; i < 8; ++i)
    {
        il << "L" << i << "():\n";
        il << "  ret " << (100 + i) << "\n";
    }
    il << "Ld():\n  ret 0\n}\n";
    writeFile(in, il.str());
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Spot-check a few cases
    EXPECT_NE(asmText.find("cmp"), std::string::npos);
    EXPECT_NE(asmText.find("#0"), std::string::npos);
    EXPECT_NE(asmText.find("#7"), std::string::npos);
    EXPECT_NE(asmText.find("b Ld"), std::string::npos);
}

TEST(Arm64CLI, SwitchDefaultOnly)
{
    const std::string in = outPath("arm64_switch_default_only.il");
    const std::string out = outPath("arm64_switch_default_only.s");
    const std::string il = "il 0.1\n"
                           "func @h(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  switch.i32 %x, ^Ld\n"
                           "Ld():\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect a direct branch to default block; no cmp/b.eq necessary
    EXPECT_NE(asmText.find("b Ld"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
