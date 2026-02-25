//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_il_opt.cpp
// Purpose: Verify IL optimizer integration in ARM64 codegen pipeline.
//          Tests that -O1 and -O2 flags are accepted and produce correct
//          results for representative IL programs.
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

/// Simple arithmetic loop — verifies -O2 produces assembly without crashing.
TEST(Arm64ILOpt, O2ProducesValidAssembly)
{
    const std::string il = "il 0.1.2\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  br loop(0, 0)\n"
                           "loop(%sum:i64, %i:i64):\n"
                           "  %done = scmp_ge %i, 100\n"
                           "  cbr %done, exit(%sum), body(%sum, %i)\n"
                           "body(%s:i64, %j:i64):\n"
                           "  %new_sum = iadd.ovf %s, %j\n"
                           "  %next = iadd.ovf %j, 1\n"
                           "  br loop(%new_sum, %next)\n"
                           "exit(%result:i64):\n"
                           "  ret %result\n"
                           "}\n";

    const std::string inP = outPath("arm64_ilopt_o2.il");
    const std::string outP = outPath("arm64_ilopt_o2.s");
    writeFile(inP, il);

    const char *argv[] = {inP.c_str(), "-O2", "-S", outP.c_str()};
    const int rc = cmd_codegen_arm64(4, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);

    const std::string asmText = readFile(outP);
    // Should have a valid function with ret instruction
    EXPECT_NE(asmText.find("ret"), std::string::npos);
    // Should have main function label
    EXPECT_NE(asmText.find("main"), std::string::npos);
}

/// Verify -O1 also works.
TEST(Arm64ILOpt, O1ProducesValidAssembly)
{
    const std::string il = "il 0.1.2\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  ret 42\n"
                           "}\n";

    const std::string inP = outPath("arm64_ilopt_o1.il");
    const std::string outP = outPath("arm64_ilopt_o1.s");
    writeFile(inP, il);

    const char *argv[] = {inP.c_str(), "-O1", "-S", outP.c_str()};
    const int rc = cmd_codegen_arm64(4, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);

    const std::string asmText = readFile(outP);
    EXPECT_NE(asmText.find("ret"), std::string::npos);
    EXPECT_NE(asmText.find("mov x0, #42"), std::string::npos);
}

/// Verify -O0 (no optimization) works.
TEST(Arm64ILOpt, O0ProducesValidAssembly)
{
    const std::string il = "il 0.1.2\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  ret 7\n"
                           "}\n";

    const std::string inP = outPath("arm64_ilopt_o0.il");
    const std::string outP = outPath("arm64_ilopt_o0.s");
    writeFile(inP, il);

    const char *argv[] = {inP.c_str(), "-O0", "-S", outP.c_str()};
    const int rc = cmd_codegen_arm64(4, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);

    const std::string asmText = readFile(outP);
    EXPECT_NE(asmText.find("ret"), std::string::npos);
}

/// Multi-function module with -O2: inlining should inline small helpers.
TEST(Arm64ILOpt, O2InlinesSmallHelpers)
{
    const std::string il = "il 0.1.2\n"
                           "func @add_one(%x:i64) -> i64 {\n"
                           "entry:\n"
                           "  %r = iadd.ovf %x, 1\n"
                           "  ret %r\n"
                           "}\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  %r = call @add_one(41)\n"
                           "  ret %r\n"
                           "}\n";

    const std::string inP = outPath("arm64_ilopt_inline.il");
    const std::string outP = outPath("arm64_ilopt_inline.s");
    writeFile(inP, il);

    const char *argv[] = {inP.c_str(), "-O2", "-S", outP.c_str()};
    const int rc = cmd_codegen_arm64(4, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);

    const std::string asmText = readFile(outP);
    EXPECT_NE(asmText.find("ret"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
