//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_print_str.cpp
// Purpose: Verify const_str + call to Viper.Console.PrintStr lower and link.
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

TEST(Arm64CLI, PrintConstStrAsm)
{
    const std::string in = outPath("arm64_print_str.il");
    const std::string out = outPath("arm64_print_str.s");
    const std::string il = "il 0.1\n"
                           "extern @Viper.Console.PrintStr(str) -> void\n"
                           "global const str @.Lmsg = \"Hello\"\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  %p = const_str @.Lmsg\n"
                           "  call @Viper.Console.PrintStr(%p)\n"
                           "  %z = alloca 8\n"
                           "  store i64, %z, 0\n"
                           "  %r = load i64, %z\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect rodata emission for the string literal
    EXPECT_NE(asmText.find(".asciz \"Hello\""), std::string::npos);
}

TEST(Arm64CLI, PrintConstStrRunNative)
{
    const std::string in = outPath("arm64_print_str_run.il");
    const std::string il = "il 0.1\n"
                           "extern @Viper.Console.PrintStr(str) -> void\n"
                           "global const str @.Lmsg = \"Hello\"\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  %p = const_str @.Lmsg\n"
                           "  call @Viper.Console.PrintStr(%p)\n"
                           "  %z = alloca 8\n"
                           "  store i64, %z, 0\n"
                           "  %r = load i64, %z\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    // Ensure we can assemble/link/run; exit code 0
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
