//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_add_imm.cpp
// Purpose: Verify add/sub immediate lowering on entry params. 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
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

TEST(Arm64CLI, AddImmParam0)
{
    const std::string in = "arm64_addimm_p0.il";
    const std::string out = "arm64_addimm_p0.s";
    const std::string il = "il 0.1\n"
                           "func @f(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %t0 = add %a, 5\n"
                           "  ret %t0\n"
                           "}\n";
    const std::string inP = outPath(in);
    const std::string outP = outPath(out);
    writeFile(inP, il);
    const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(outP);
    EXPECT_NE(asmText.find("add x0, x0, #5"), std::string::npos);
}

TEST(Arm64CLI, SubImmParam1)
{
    const std::string in = "arm64_subimm_p1.il";
    const std::string out = "arm64_subimm_p1.s";
    const std::string il = "il 0.1\n"
                           "func @g(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %t0 = sub %b, 3\n"
                           "  ret %t0\n"
                           "}\n";
    const std::string inP2 = outPath(in);
    const std::string outP2 = outPath(out);
    writeFile(inP2, il);
    const char *argv[] = {inP2.c_str(), "-S", outP2.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(outP2);
    // Expect move param1 to x0 then sub immediate
    EXPECT_NE(asmText.find("mov x0, x1"), std::string::npos);
    EXPECT_NE(asmText.find("sub x0, x0, #3"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
