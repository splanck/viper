//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_select.cpp
// Purpose: Verify select-like patterns using cbr + join with phi via edge moves.
//          Covers simple constants and values loaded from memory.
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

TEST(Arm64CLI, Select_ConstArms)
{
    const std::string in = outPath("arm64_select_const.il");
    const std::string out = outPath("arm64_select_const.s");
    const std::string il =
        "il 0.1\n"
        "func @f(%x:i64) -> i64 {\n"
        "entry(%x:i64):\n"
        "  %cond = scmp_gt %x, 0\n"
        "  cbr %cond, then, els\n"
        "then():\n"
        "  br join(1)\n"
        "els():\n"
        "  br join(0)\n"
        "join(%v:i64):\n"
        "  ret %v\n"
        "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect compare, conditional branch, and movs for phi edge copies; no stack traffic
    EXPECT_NE(asmText.find("cmp"), std::string::npos);
    EXPECT_NE(asmText.find("b."), std::string::npos);
    EXPECT_NE(asmText.find(" mov x"), std::string::npos);
    EXPECT_EQ(asmText.find(" str x"), std::string::npos);
    EXPECT_EQ(asmText.find(" ldr x"), std::string::npos);
}

TEST(Arm64CLI, Select_LoadArms)
{
    const std::string in = outPath("arm64_select_load.il");
    const std::string out = outPath("arm64_select_load.s");
    const std::string il =
        "il 0.1\n"
        "func @g(%x:i64) -> i64 {\n"
        "entry(%x:i64):\n"
        "  %a = alloca 8\n"
        "  %b = alloca 8\n"
        "  store i64, %a, 11\n"
        "  store i64, %b, 22\n"
        "  %cond = scmp_gt %x, 0\n"
        "  cbr %cond, then, els\n"
        "then():\n"
        "  %av = load i64, %a\n"
        "  br join(%av)\n"
        "els():\n"
        "  %bv = load i64, %b\n"
        "  br join(%bv)\n"
        "join(%v:i64):\n"
        "  ret %v\n"
        "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect str to locals, loads via [fp,#off], conditional branch, and movs for phi edge copies
    EXPECT_NE(asmText.find("str x"), std::string::npos);
    EXPECT_NE(asmText.find("ldr x"), std::string::npos);
    EXPECT_NE(asmText.find("b."), std::string::npos);
    EXPECT_NE(asmText.find(" mov x"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}

