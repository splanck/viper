//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_call_mid_block.cpp
// Purpose: Verify a mid-function call with result stored/loaded later.
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

/// @brief Returns the expected mangled symbol name for a call target.
static std::string blSym(const std::string &name)
{
#if defined(__APPLE__)
    return "bl _" + name;
#else
    return "bl " + name;
#endif
}

TEST(Arm64CLI, CallMidFunction_ResultReused)
{
    const std::string in = outPath("arm64_call_mid.il");
    const std::string out = outPath("arm64_call_mid.s");
    const std::string il = "il 0.1\n"
                           "extern @twice(i64) -> i64\n"
                           "func @f(%a:i64) -> i64 {\n"
                           "entry(%a:i64):\n"
                           "  %L = alloca 8\n"
                           "  %c = call @twice(%a)\n"
                           "  store i64, %L, %c\n"
                           "  %v = load i64, %L\n"
                           "  %r = add %v, 1\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect call, store to FP-rel local, later load and add
    EXPECT_NE(asmText.find(blSym("twice")), std::string::npos);
    // Store to FP-relative local (may be from x0 directly or via a vreg)
    EXPECT_NE(asmText.find("str x"), std::string::npos);
    EXPECT_NE(asmText.find("[x29, #"), std::string::npos);
    EXPECT_NE(asmText.find("ldr x"), std::string::npos);
    EXPECT_NE(asmText.find("add x"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
