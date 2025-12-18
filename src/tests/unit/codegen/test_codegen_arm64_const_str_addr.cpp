//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_const_str_addr.cpp
// Purpose: Verify AArch64 materializes addresses for const_str/globals and emits rodata.
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

TEST(Arm64CLI, ConstStr_AddressMaterialization)
{
    const std::string in = "arm64_cli_const_str.il";
    const std::string out = "arm64_cli_const_str.s";
    const std::string il = "il 0.1\n"
                           "global const str @.Lmsg = \"hi\"\n"
                           "func @get() -> ptr {\n"
                           "entry:\n"
                           "  %p = const_str @.Lmsg\n"
                           "  ret %p\n"
                           "}\n";

    const std::string inP = outPath(in);
    const std::string outP = outPath(out);
    writeFile(inP, il);

    const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
    const int rc = cmd_codegen_arm64(3, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);

    const std::string asmText = readFile(outP);
    // Expect rodata section with pooled label and ascii contents
    EXPECT_NE(asmText.find(".section"), std::string::npos);
    EXPECT_NE(asmText.find(".asciz \"hi\""), std::string::npos);
    // Expect adrp/add page materialization in function
    EXPECT_NE(asmText.find("adrp x"), std::string::npos);
    EXPECT_NE(asmText.find("@PAGE"), std::string::npos);
    EXPECT_NE(asmText.find("@PAGEOFF"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
