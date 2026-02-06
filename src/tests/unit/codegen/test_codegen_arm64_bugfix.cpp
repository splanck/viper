//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_bugfix.cpp
// Purpose: Regression tests for ARM64 codegen bug fixes #1, #2, #3.
//
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <filesystem>
#include <fstream>
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

/// Bug #3: void main should exit with code 0, not whatever was in x0.
TEST(Arm64Bugfix, VoidMainExitZero)
{
    const std::string in = outPath("arm64_bugfix_void_main.il");
    // A void main that calls a runtime function leaving a non-zero value in x0.
    // Before the fix, this would exit with whatever rt_term_say left in x0.
    const std::string il = "il 0.2.0\n"
                           "extern @Viper.Terminal.Say(str) -> void\n"
                           "global const str @.msg = \"hello\"\n"
                           "func @main() -> void {\n"
                           "entry_0:\n"
                           "  %t0 = const_str @.msg\n"
                           "  call @Viper.Terminal.Say(%t0)\n"
                           "  ret\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);
}

/// Bug #1: Boolean return values should be masked to i1 (0 or 1).
/// Tests that a runtime function returning bool is correctly captured.
TEST(Arm64Bugfix, BoolReturnMasked)
{
    const std::string in = outPath("arm64_bugfix_bool_return.il");
    // Calls rt_str_eq which returns bool (i1). If the masking works,
    // the comparison and conditional branch should function correctly.
    const std::string il = "il 0.2.0\n"
                           "extern @Viper.String.Equals(str, str) -> i1\n"
                           "global const str @.a = \"hello\"\n"
                           "global const str @.b = \"hello\"\n"
                           "func @main() -> i64 {\n"
                           "entry_0:\n"
                           "  %t0 = const_str @.a\n"
                           "  %t1 = const_str @.b\n"
                           "  %t2 = call @Viper.String.Equals(%t0, %t1)\n"
                           "  cbr %t2, yes_0, no_0\n"
                           "yes_0:\n"
                           "  ret 0\n"
                           "no_0:\n"
                           "  ret 1\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    // Equal strings should return exit code 0 (took the yes branch)
    ASSERT_EQ(rc, 0);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
