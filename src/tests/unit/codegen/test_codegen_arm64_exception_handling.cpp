//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_exception_handling.cpp
// Purpose: Comprehensive EH opcode tests for AArch64 backend.
// Key invariants: EH markers lower to runtime helper calls.
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

// Test 1: eh.push alone
TEST(Arm64EH, EhPush)
{
    const std::string in = outPath("arm64_eh_push.il");
    const std::string out = outPath("arm64_eh_push.s");
    const std::string il = "il 0.1\n"
                           "func @f() -> i64 {\n"
                           "entry:\n"
                           "  eh.push ^handler\n"
                           "  ret 0\n"
                           "handler ^handler(%err:Error, %tok:ResumeTok):\n"
                           "  eh.entry\n"
                           "  ret 1\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should compile without error
    EXPECT_FALSE(asmText.empty());
    // EH handlers may call runtime helpers
    // Verification: code generated
    EXPECT_NE(asmText.find("ret"), std::string::npos);
}

// Test 2: eh.pop - pop error handler
TEST(Arm64EH, EhPop)
{
    const std::string in = outPath("arm64_eh_pop.il");
    const std::string out = outPath("arm64_eh_pop.s");
    const std::string il = "il 0.1\n"
                           "func @f() -> i64 {\n"
                           "entry:\n"
                           "  eh.push ^handler\n"
                           "  eh.pop\n"
                           "  ret 0\n"
                           "handler ^handler(%err:Error, %tok:ResumeTok):\n"
                           "  eh.entry\n"
                           "  ret 1\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_FALSE(asmText.empty());
}

// Test 3: trap instruction
TEST(Arm64EH, Trap)
{
    const std::string in = outPath("arm64_eh_trap.il");
    const std::string out = outPath("arm64_eh_trap.s");
    const std::string il = "il 0.1\n"
                           "func @f() -> i64 {\n"
                           "entry:\n"
                           "  trap\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect call to trap helper
    EXPECT_NE(asmText.find(blSym("rt_trap")), std::string::npos);
}

// Test 4: trap with error code
TEST(Arm64EH, TrapFromErr)
{
    const std::string in = outPath("arm64_eh_trap_err.il");
    const std::string out = outPath("arm64_eh_trap_err.s");
    const std::string il = "il 0.1\n"
                           "func @f(%code:i64) -> i64 {\n"
                           "entry(%code:i64):\n"
                           "  trap.from_err i32 %code\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect call to trap helper
    EXPECT_NE(asmText.find(blSym("rt_trap")), std::string::npos);
}

// Test 5: resume.same - resume at same point
TEST(Arm64EH, ResumeSame)
{
    const std::string in = outPath("arm64_eh_resume_same.il");
    const std::string out = outPath("arm64_eh_resume_same.s");
    const std::string il = "il 0.1\n"
                           "func @f() -> i64 {\n"
                           "entry:\n"
                           "  eh.push ^handler\n"
                           "  trap\n"
                           "handler ^handler(%err:Error, %tok:ResumeTok):\n"
                           "  eh.entry\n"
                           "  resume.same %tok\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_FALSE(asmText.empty());
}

// Test 6: resume.next - resume at next point
TEST(Arm64EH, ResumeNext)
{
    const std::string in = outPath("arm64_eh_resume_next.il");
    const std::string out = outPath("arm64_eh_resume_next.s");
    const std::string il = "il 0.1\n"
                           "func @f() -> i64 {\n"
                           "entry:\n"
                           "  eh.push ^handler\n"
                           "  trap.from_err i32 1\n"
                           "after:\n"
                           "  eh.pop\n"
                           "  ret 0\n"
                           "handler ^handler(%err:Error, %tok:ResumeTok):\n"
                           "  eh.entry\n"
                           "  resume.next %tok\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_FALSE(asmText.empty());
}

// Test 7: Full try-catch pattern
TEST(Arm64EH, TryCatchPattern)
{
    const std::string in = outPath("arm64_eh_try_catch.il");
    const std::string out = outPath("arm64_eh_try_catch.s");
    const std::string il = "il 0.1\n"
                           "extern @may_throw(i64) -> i64\n"
                           "func @try_catch(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  eh.push ^catch\n"
                           "  %r = call @may_throw(%x)\n"
                           "  eh.pop\n"
                           "  ret %r\n"
                           "catch ^catch(%err:Error, %tok:ResumeTok):\n"
                           "  eh.entry\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have the call
    EXPECT_NE(asmText.find("bl "), std::string::npos);
}

// Test 8: Nested exception handlers
TEST(Arm64EH, NestedHandlers)
{
    const std::string in = outPath("arm64_eh_nested.il");
    const std::string out = outPath("arm64_eh_nested.s");
    const std::string il = "il 0.1\n"
                           "func @nested() -> i64 {\n"
                           "entry:\n"
                           "  eh.push ^outer\n"
                           "  eh.push ^inner\n"
                           "  trap.from_err i32 1\n"
                           "inner ^inner(%e1:Error, %t1:ResumeTok):\n"
                           "  eh.entry\n"
                           "  eh.pop\n"
                           "  ret 1\n"
                           "outer ^outer(%e2:Error, %t2:ResumeTok):\n"
                           "  eh.entry\n"
                           "  ret 2\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_FALSE(asmText.empty());
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
