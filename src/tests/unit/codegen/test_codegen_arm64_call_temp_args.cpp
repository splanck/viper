//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_call_temp_args.cpp
// Purpose: Verify CLI lowers calls with a single non-entry temp argument by computing into X9.
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

/// @brief Returns the expected mangled symbol name for a call target.
static std::string blSym(const std::string &name)
{
#if defined(__APPLE__)
    return "bl _" + name;
#else
    return "bl " + name;
#endif
}

TEST(Arm64CLI, CallWithTempRR)
{
    const std::string in = outPath("arm64_call_temp_rr.il");
    const std::string out = outPath("arm64_call_temp_rr.s");
    const std::string il = "il 0.1\n"
                           "extern @h(i64, i64) -> i64\n"
                           "func @f(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %t1 = add %a, %b\n"
                           "  %t0 = call @h(%t1, %a)\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect add into x9 and then move to x0
    EXPECT_NE(asmText.find("add x9, x0, x1"), std::string::npos);
    EXPECT_NE(asmText.find("mov x0, x9"), std::string::npos);
    EXPECT_NE(asmText.find(blSym("h")), std::string::npos);
}

TEST(Arm64CLI, CallWithTempRI)
{
    const std::string in = outPath("arm64_call_temp_ri.il");
    const std::string out = outPath("arm64_call_temp_ri.s");
    const std::string il = "il 0.1\n"
                           "extern @h(i64, i64) -> i64\n"
                           "func @f(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %t1 = add %b, 5\n"
                           "  %t0 = call @h(%a, %t1)\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("add x9, x1, #5"), std::string::npos);
    EXPECT_NE(asmText.find("mov x1, x9"), std::string::npos);
    EXPECT_NE(asmText.find(blSym("h")), std::string::npos);
}

TEST(Arm64CLI, CallWithTempShift)
{
    const std::string in = outPath("arm64_call_temp_shl.il");
    const std::string out = outPath("arm64_call_temp_shl.s");
    const std::string il = "il 0.1\n"
                           "extern @h(i64, i64) -> i64\n"
                           "func @f(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %t1 = shl %a, 3\n"
                           "  %t0 = call @h(%t1, %b)\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("lsl x9, x0, #3"), std::string::npos);
    EXPECT_NE(asmText.find("mov x0, x9"), std::string::npos);
    EXPECT_NE(asmText.find(blSym("h")), std::string::npos);
}

TEST(Arm64CLI, CallWithCompareTemp)
{
    const std::string in = outPath("arm64_call_temp_cmp.il");
    const std::string out = outPath("arm64_call_temp_cmp.s");
    const std::string il = "il 0.1\n"
                           "extern @h(i64, i64) -> i64\n"
                           "func @f(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %t1 = icmp_eq %a, %b\n"
                           "  %t0 = call @h(%t1, %a)\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("cmp x0, x1"), std::string::npos);
    EXPECT_NE(asmText.find("cset x9, eq"), std::string::npos);
    EXPECT_NE(asmText.find("mov x0, x9"), std::string::npos);
    EXPECT_NE(asmText.find(blSym("h")), std::string::npos);
}

TEST(Arm64CLI, CallWithTwoTemps)
{
    const std::string in = outPath("arm64_call_two_temps.il");
    const std::string out = outPath("arm64_call_two_temps.s");
    const std::string il = "il 0.1\n"
                           "extern @h(i64, i64) -> i64\n"
                           "func @f(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %t1 = add %a, %b\n"
                           "  %t2 = shl %b, 1\n"
                           "  %t0 = call @h(%t1, %t2)\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect both temps computed into x9/x10 then moved to x0/x1
    EXPECT_NE(asmText.find("add x9, x0, x1"), std::string::npos);
    EXPECT_NE(asmText.find("lsl x10, x1, #1"), std::string::npos);
    EXPECT_NE(asmText.find("mov x0, x9"), std::string::npos);
    EXPECT_NE(asmText.find("mov x1, x10"), std::string::npos);
    EXPECT_NE(asmText.find(blSym("h")), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
