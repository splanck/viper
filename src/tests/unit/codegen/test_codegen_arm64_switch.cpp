//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_switch.cpp
// Purpose: Verify AArch64 lowering for IL switch.i32 into cmp + b.eq chains.
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
#include <vector>

#include "tools/viper/cmd_codegen_arm64.hpp"

using namespace viper::tools::ilc;

static std::string outPath(const std::string &name) {
    namespace fs = std::filesystem;
    const fs::path dir{"build/test-out/arm64"};
    fs::create_directories(dir);
    return (dir / name).string();
}

static void writeFile(const std::string &path, const std::string &text) {
    std::ofstream ofs(path);
    ASSERT_TRUE(static_cast<bool>(ofs));
    ofs << text;
}

static std::string readFile(const std::string &path) {
    std::ifstream ifs(path);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static int runArm64(std::initializer_list<const char *> args) {
    std::vector<char *> argv;
    for (const char *arg : args)
        argv.push_back(const_cast<char *>(arg));
    return cmd_codegen_arm64(static_cast<int>(argv.size()), argv.data());
}

TEST(Arm64CLI, SwitchSmall) {
    const std::string in = outPath("arm64_switch_small.il");
    const std::string out = outPath("arm64_switch_small.s");
    const std::string il = "il 0.1\n"
                           "func @f(%x:i32) -> i64 {\n"
                           "entry(%x:i32):\n"
                           "  switch.i32 %x, ^Ld, 1 -> ^L1, 2 -> ^L2\n"
                           "L1():\n"
                           "  ret 10\n"
                           "L2():\n"
                           "  ret 20\n"
                           "Ld():\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str(), "-O0"};
    ASSERT_EQ(cmd_codegen_arm64(4, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect cmp <reg>, #1; b.eq L1 and cmp <reg>, #2; b.eq L2 then branch to default
    EXPECT_NE(asmText.find("cmp"), std::string::npos);
    EXPECT_NE(asmText.find("#1"), std::string::npos);
    EXPECT_NE(asmText.find("b.eq LL1"), std::string::npos);
    EXPECT_NE(asmText.find("#2"), std::string::npos);
    EXPECT_NE(asmText.find("b.eq LL2"), std::string::npos);
    EXPECT_NE(asmText.find("b LLd"), std::string::npos);
}

TEST(Arm64CLI, SwitchMany) {
    const std::string in = outPath("arm64_switch_many.il");
    const std::string out = outPath("arm64_switch_many.s");
    std::ostringstream il;
    il << "il 0.1\n";
    il << "func @g(%x:i32) -> i64 {\n";
    il << "entry(%x:i32):\n";
    il << "  switch.i32 %x, ^Ld";
    for (int i = 0; i < 8; ++i)
        il << ", " << i << " -> ^L" << i;
    il << "\n";
    for (int i = 0; i < 8; ++i) {
        il << "L" << i << "():\n";
        il << "  ret " << (100 + i) << "\n";
    }
    il << "Ld():\n  ret 0\n}\n";
    writeFile(in, il.str());
    const char *argv[] = {in.c_str(), "-S", out.c_str(), "-O0"};
    ASSERT_EQ(cmd_codegen_arm64(4, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Large switches should use helper labels for the binary-search decision tree.
    EXPECT_NE(asmText.find("switch_tree"), std::string::npos);
    EXPECT_NE(asmText.find("switch_left"), std::string::npos);
    EXPECT_NE(asmText.find("switch_right"), std::string::npos);
    EXPECT_NE(asmText.find("b LLd"), std::string::npos);
}

TEST(Arm64CLI, SwitchDefaultOnly) {
    const std::string in = outPath("arm64_switch_default_only.il");
    const std::string out = outPath("arm64_switch_default_only.s");
    const std::string il = "il 0.1\n"
                           "func @h(%x:i32) -> i64 {\n"
                           "entry(%x:i32):\n"
                           "  switch.i32 %x, ^Ld\n"
                           "Ld():\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str(), "-O0"};
    ASSERT_EQ(cmd_codegen_arm64(4, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // For a switch with only a default, the compiler may emit either:
    // 1. An explicit branch "b Ld" to the default block
    // 2. A fallthrough to the default block (no branch needed if immediately adjacent)
    // Both are correct; we just verify the default label exists
    EXPECT_NE(asmText.find("LLd:"), std::string::npos);
}

TEST(Arm64CLI, SwitchCaseArgsPreserveTakenEdgeValues) {
    const std::string in = outPath("arm64_switch_case_args.il");
    const std::string il = "il 0.1\n"
                           "func @dispatch(%x:i32) -> i64 {\n"
                           "entry(%x:i32):\n"
                           "  switch.i32 %x, ^Ld(50, 5), 1 -> ^L1(10, 1), 2 -> ^L2(20, 2)\n"
                           "L1(%a:i64, %b:i64):\n"
                           "  %r1 = iadd.ovf %a, %b\n"
                           "  ret %r1\n"
                           "L2(%a:i64, %b:i64):\n"
                           "  %r2 = iadd.ovf %a, %b\n"
                           "  ret %r2\n"
                           "Ld(%a:i64, %b:i64):\n"
                           "  %rd = iadd.ovf %a, %b\n"
                           "  ret %rd\n"
                           "}\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  %x:i32 = cast.si_narrow.chk 2\n"
                           "  %r = call @dispatch(%x)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    ASSERT_EQ(runArm64({in.c_str(), "-run-native", "-O0"}), 22);
}

TEST(Arm64CLI, SwitchDefaultArgsPreserveDefaultEdgeValues) {
    const std::string in = outPath("arm64_switch_default_args.il");
    const std::string il = "il 0.1\n"
                           "func @dispatch(%x:i32) -> i64 {\n"
                           "entry(%x:i32):\n"
                           "  switch.i32 %x, ^Ld(50, 5), 1 -> ^L1(10, 1), 2 -> ^L2(20, 2)\n"
                           "L1(%a:i64, %b:i64):\n"
                           "  %r1 = iadd.ovf %a, %b\n"
                           "  ret %r1\n"
                           "L2(%a:i64, %b:i64):\n"
                           "  %r2 = iadd.ovf %a, %b\n"
                           "  ret %r2\n"
                           "Ld(%a:i64, %b:i64):\n"
                           "  %rd = iadd.ovf %a, %b\n"
                           "  ret %rd\n"
                           "}\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  %x:i32 = cast.si_narrow.chk 7\n"
                           "  %r = call @dispatch(%x)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    ASSERT_EQ(runArm64({in.c_str(), "-run-native", "-O0"}), 55);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
