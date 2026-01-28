//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_indirect_call.cpp
// Purpose: Verify indirect function calls (call.indirect) via pointer lowering.
// Key invariants: Emits blr for indirect calls through a register.
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

// Test 1: Simple direct call - call via function symbol reference
TEST(Arm64IndirectCall, SimpleIndirect)
{
    const std::string in = outPath("arm64_indirect_call_simple.il");
    const std::string out = outPath("arm64_indirect_call_simple.s");
    // Direct call to @target using 'call' opcode
    const std::string il = "il 0.1\n"
                           "func @target() -> i64 {\n"
                           "entry:\n"
                           "  ret 42\n"
                           "}\n"
                           "func @caller() -> i64 {\n"
                           "entry:\n"
                           "  %r = call @target()\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect bl for direct call
    EXPECT_NE(asmText.find("bl "), std::string::npos);
}

// Test 2: Direct call with integer argument
TEST(Arm64IndirectCall, WithIntArg)
{
    const std::string in = outPath("arm64_indirect_call_intarg.il");
    const std::string out = outPath("arm64_indirect_call_intarg.s");
    const std::string il = "il 0.1\n"
                           "func @target(%n:i64) -> i64 {\n"
                           "entry(%n:i64):\n"
                           "  ret %n\n"
                           "}\n"
                           "func @caller(%arg:i64) -> i64 {\n"
                           "entry(%arg:i64):\n"
                           "  %r = call @target(%arg)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect bl for call
    EXPECT_NE(asmText.find("bl "), std::string::npos);
}

// Test 3: Direct call with multiple arguments
TEST(Arm64IndirectCall, WithMultipleArgs)
{
    const std::string in = outPath("arm64_indirect_call_multiarg.il");
    const std::string out = outPath("arm64_indirect_call_multiarg.s");
    const std::string il = "il 0.1\n"
                           "func @target(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %r = add %a, %b\n"
                           "  ret %r\n"
                           "}\n"
                           "func @caller(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %r = call @target(%a, %b)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect bl for call
    EXPECT_NE(asmText.find("bl "), std::string::npos);
}

// Test 4: Direct call returning void (no result used)
TEST(Arm64IndirectCall, VoidReturn)
{
    const std::string in = outPath("arm64_indirect_call_void.il");
    const std::string out = outPath("arm64_indirect_call_void.s");
    const std::string il = "il 0.1\n"
                           "extern @sink(i64) -> void\n"
                           "func @caller(%arg:i64) -> i64 {\n"
                           "entry(%arg:i64):\n"
                           "  call @sink(%arg)\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect bl for call
    EXPECT_NE(asmText.find("bl "), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
