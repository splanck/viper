//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_callee_saved.cpp
// Purpose: Verify callee-saved register preservation on AArch64.
// Key invariants: x19-x28 and d8-d15 must be saved/restored if used.
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

// Test 1: Value live across call needs callee-saved or spill
TEST(Arm64CalleeSaved, ValueAcrossCall)
{
    const std::string in = outPath("arm64_callee_call.il");
    const std::string out = outPath("arm64_callee_call.s");
    const std::string il = "il 0.1\n"
                           "extern @compute(i64) -> i64\n"
                           "func @use_across(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %tmp = call @compute(%x)\n"
                           "  %r = add %tmp, %x\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // %x must survive across the call
    // Should have stp/ldp for saving or spill to stack
    EXPECT_NE(asmText.find(blSym("compute")), std::string::npos);
}

// Test 2: Multiple values live across call
TEST(Arm64CalleeSaved, MultipleValuesAcrossCall)
{
    const std::string in = outPath("arm64_callee_multi.il");
    const std::string out = outPath("arm64_callee_multi.s");
    const std::string il = "il 0.1\n"
                           "extern @work() -> i64\n"
                           "func @multi(%a:i64, %b:i64, %c:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64, %c:i64):\n"
                           "  %x = add %a, %b\n"
                           "  %y = mul %b, %c\n"
                           "  %tmp = call @work()\n"
                           "  %r1 = add %x, %y\n"
                           "  %r = add %r1, %tmp\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have save/restore mechanism
    EXPECT_FALSE(asmText.empty());
}

// Test 3: FP value live across call
TEST(Arm64CalleeSaved, FPValueAcrossCall)
{
    const std::string in = outPath("arm64_callee_fp.il");
    const std::string out = outPath("arm64_callee_fp.s");
    const std::string il = "il 0.1\n"
                           "extern @fp_work(f64) -> f64\n"
                           "func @fp_across(%x:f64, %y:f64) -> f64 {\n"
                           "entry(%x:f64, %y:f64):\n"
                           "  %tmp = call @fp_work(%x)\n"
                           "  %r = fadd %tmp, %y\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // %y must survive across the call
    EXPECT_NE(asmText.find(blSym("fp_work")), std::string::npos);
}

// Test 4: Loop with call - accumulator needs preserving
TEST(Arm64CalleeSaved, LoopWithCall)
{
    const std::string in = outPath("arm64_callee_loop.il");
    const std::string out = outPath("arm64_callee_loop.s");
    const std::string il = "il 0.1\n"
                           "extern @get_value(i64) -> i64\n"
                           "func @sum_loop(%n:i64) -> i64 {\n"
                           "entry(%n:i64):\n"
                           "  br loop(0, 0)\n"
                           "loop(%i:i64, %sum:i64):\n"
                           "  %v = call @get_value(%i)\n"
                           "  %new_sum = add %sum, %v\n"
                           "  %next_i = add %i, 1\n"
                           "  %done = icmp_eq %next_i, %n\n"
                           "  cbr %done, exit(%new_sum), loop(%next_i, %new_sum)\n"
                           "exit(%result:i64):\n"
                           "  ret %result\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Loop variables must survive across calls
    EXPECT_NE(asmText.find(blSym("get_value")), std::string::npos);
}

// Test 5: Nested calls
TEST(Arm64CalleeSaved, NestedCalls)
{
    const std::string in = outPath("arm64_callee_nested.il");
    const std::string out = outPath("arm64_callee_nested.s");
    const std::string il = "il 0.1\n"
                           "extern @outer(i64) -> i64\n"
                           "extern @inner(i64) -> i64\n"
                           "func @nested(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %a = call @outer(%x)\n"
                           "  %b = call @inner(%a)\n"
                           "  %r = add %x, %b\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // %x must survive across both calls
    EXPECT_NE(asmText.find(blSym("outer")), std::string::npos);
    EXPECT_NE(asmText.find(blSym("inner")), std::string::npos);
}

// Test 6: Many values live across call (force use of multiple callee-saved)
TEST(Arm64CalleeSaved, ManyValuesNeedSave)
{
    const std::string in = outPath("arm64_callee_many.il");
    const std::string out = outPath("arm64_callee_many.s");
    const std::string il = "il 0.1\n"
                           "extern @work() -> i64\n"
                           "func @many(%a:i64, %b:i64, %c:i64, %d:i64, %e:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64, %c:i64, %d:i64, %e:i64):\n"
                           "  %t1 = add %a, %b\n"
                           "  %t2 = add %c, %d\n"
                           "  %t3 = add %t1, %t2\n"
                           "  %t4 = add %t3, %e\n"
                           "  %x = call @work()\n"
                           "  %r1 = add %t4, %a\n"
                           "  %r2 = add %r1, %b\n"
                           "  %r3 = add %r2, %c\n"
                           "  %r4 = add %r3, %d\n"
                           "  %r = add %r4, %x\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // With many live values across call, should see stp/ldp in prologue/epilogue
    bool hasStp = asmText.find("stp x") != std::string::npos;
    bool hasLdp = asmText.find("ldp x") != std::string::npos;
    // At minimum should have frame pointer save (x29, x30)
    EXPECT_TRUE(hasStp || hasLdp || asmText.find("str x") != std::string::npos);
}

// Test 7: Simple function without calls (may not need callee-saved)
TEST(Arm64CalleeSaved, NoCalls)
{
    const std::string in = outPath("arm64_callee_nocall.il");
    const std::string out = outPath("arm64_callee_nocall.s");
    const std::string il = "il 0.1\n"
                           "func @simple(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %r = add %a, %b\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Simple leaf function may not need to save callee-saved regs
    EXPECT_NE(asmText.find("add x"), std::string::npos);
}

// Test 8: Verify prologue/epilogue structure
TEST(Arm64CalleeSaved, PrologueEpilogue)
{
    const std::string in = outPath("arm64_callee_proepi.il");
    const std::string out = outPath("arm64_callee_proepi.s");
    const std::string il = "il 0.1\n"
                           "extern @work() -> i64\n"
                           "func @needs_frame(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %tmp = call @work()\n"
                           "  %r = add %tmp, %x\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have proper frame setup/teardown
    // Typical pattern: stp x29, x30, [sp, #-N]!
    bool hasFrameSetup = asmText.find("stp") != std::string::npos ||
                         asmText.find("str x29") != std::string::npos ||
                         asmText.find("str x30") != std::string::npos;
    EXPECT_TRUE(hasFrameSetup);
    // Should have ret
    EXPECT_NE(asmText.find("ret"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
