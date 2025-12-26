//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_trap_eh.cpp
// Purpose: Verify AArch64 lowering for IL traps and EH markers.
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

TEST(Arm64CLI, TrapSimple)
{
    const std::string in = outPath("arm64_trap.il");
    const std::string out = outPath("arm64_trap.s");
    const std::string il = "il 0.1\n"
                           "func @t() -> i64 {\n"
                           "entry:\n"
                           "  trap\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find(blSym("rt_trap")), std::string::npos);
}

TEST(Arm64CLI, TrapFromErr)
{
    const std::string in = outPath("arm64_trap_from_err.il");
    const std::string out = outPath("arm64_trap_from_err.s");
    const std::string il = "il 0.1\n"
                           "func @te(%c:i64) -> i64 {\n"
                           "entry(%c:i64):\n"
                           "  trap.from_err i32 %c\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find(blSym("rt_trap")), std::string::npos);
}

TEST(Arm64CLI, EhMarkersNoop)
{
    const std::string in = outPath("arm64_eh.il");
    const std::string out = outPath("arm64_eh.s");
    const std::string il = "il 0.1\n"
                           "func @errors_demo() -> i64 {\n"
                           "entry:\n"
                           "  eh.push ^handle\n"
                           "  trap.from_err i32 6\n"
                           "handler ^handle(%err:Error, %tok:ResumeTok):\n"
                           "  eh.entry\n"
                           "  resume.same %tok\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // We should see the trap helper call; EH markers produce no extra code.
    EXPECT_NE(asmText.find(blSym("rt_trap")), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
