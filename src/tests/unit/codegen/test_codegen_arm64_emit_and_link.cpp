//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_emit_and_link.cpp
// Purpose: Verify -S writes .s and -o links an executable for arm64 CLI.
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include <filesystem>
#include <fstream>
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

TEST(Arm64CLI, EmitAsmAndLinkExe)
{
    namespace fs = std::filesystem;
    const std::string in = outPath("emit_link.il");
    const std::string asmOut = outPath("emit_link.s");
    const std::string exeOut = outPath("emit_link_exe");
    const std::string il =
        "il 0.1\n"
        "func @main() -> i64 {\n"
        "entry:\n"
        "  ret 0\n"
        "}\n";
    writeFile(in, il);
    {
        const char *argv[] = {in.c_str(), "-S", asmOut.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        ASSERT_TRUE(fs::exists(asmOut));
    }
    {
        const char *argv[] = {in.c_str(), "-o", exeOut.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        ASSERT_TRUE(fs::exists(exeOut));
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}

