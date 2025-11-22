//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_large_imm.cpp
// Purpose: Verify large immediate materialization uses movz/movk sequence for ret const. 
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

TEST(Arm64CLI, LargeImmRet)
{
    const std::string in = "arm64_large_imm.il";
    const std::string out = "arm64_large_imm.s";
    // Use a value that requires multiple 16-bit chunks.
    const std::string il = "il 0.1\n\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  ret 81985529216486895\n" // 0x0123456789ABCDEF
                           "}\n";
    const std::string inP = outPath(in);
    const std::string outP = outPath(out);
    writeFile(inP, il);
    const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(outP);
    EXPECT_NE(asmText.find("movz x0, #"), std::string::npos);
    EXPECT_NE(asmText.find("movk x0, #"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
