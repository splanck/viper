//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_call_many_args.cpp
// Purpose: Verify AArch64 lowering handles >8 integer args with outgoing stack area
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

TEST(Arm64CLI, CallManyArgs_WithParamsConstsAndLoad)
{
    const std::string in = outPath("arm64_call_many.il");
    const std::string out = outPath("arm64_call_many.s");
    // 10 args: mix of params, constants, and a local load
    // f(a,b,c,d,e,f,g,h,i,j) -> call h(a, 1, b, 2, c, 3, d, 4, e, load local)
    const std::string il = "il 0.1\n"
                           "extern @h(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64) -> i64\n"
                           "func @f(%a:i64, %b:i64, %c:i64, %d:i64, %e:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64, %c:i64, %d:i64, %e:i64):\n"
                           "  %L = alloca 8\n"
                           "  store %e, %L\n"
                           "  %tmp = load %L\n"
                           "  %r = call @h(%a, 1, %b, 2, %c, 3, %d, 4, %e, %tmp)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // First eight in x0..x7
    EXPECT_NE(asmText.find("bl h"), std::string::npos);
    EXPECT_NE(asmText.find("mov x0, x0"),
              std::string::npos); // a -> x0 (may be elided if already x0)
    EXPECT_NE(asmText.find("mov x1, #1"), std::string::npos);
    EXPECT_NE(asmText.find("mov x2, x1"), std::string::npos); // b -> x2
    EXPECT_NE(asmText.find("mov x3, #2"), std::string::npos);
    EXPECT_NE(asmText.find("mov x4, x2"), std::string::npos); // c -> x4 (param order)
    EXPECT_NE(asmText.find("mov x5, #3"), std::string::npos);
    EXPECT_NE(asmText.find("mov x6, x3"), std::string::npos); // d -> x6
    EXPECT_NE(asmText.find("mov x7, #4"), std::string::npos);
    // Stack args: offsets 0 and 8 (for 9th and 10th args)
    EXPECT_NE(asmText.find("str x"), std::string::npos);
    EXPECT_NE(asmText.find("[sp, #0]"), std::string::npos);
    EXPECT_NE(asmText.find("[sp, #8]"), std::string::npos);
}

TEST(Arm64CLI, CallZeroAndOneArg)
{
    // Zero args
    {
        const std::string in = outPath("arm64_call_zero.il");
        const std::string out = outPath("arm64_call_zero.s");
        const std::string il = "il 0.1\n"
                               "extern @g() -> i64\n"
                               "func @f() -> i64 {\n"
                               "entry:\n"
                               "  %r = call @g()\n"
                               "  ret %r\n"
                               "}\n";
        writeFile(in, il);
        const char *argv[] = {in.c_str(), "-S", out.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(out);
        EXPECT_NE(asmText.find("bl g"), std::string::npos);
    }
    // One arg constant
    {
        const std::string in = outPath("arm64_call_one.il");
        const std::string out = outPath("arm64_call_one.s");
        const std::string il = "il 0.1\n"
                               "extern @g(i64) -> i64\n"
                               "func @f() -> i64 {\n"
                               "entry:\n"
                               "  %r = call @g(42)\n"
                               "  ret %r\n"
                               "}\n";
        writeFile(in, il);
        const char *argv[] = {in.c_str(), "-S", out.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(out);
        EXPECT_NE(asmText.find("mov x0, #42"), std::string::npos);
        EXPECT_NE(asmText.find("bl g"), std::string::npos);
    }
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
