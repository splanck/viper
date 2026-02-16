//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_callee_stack_params.cpp
// Purpose: Verify callee correctly loads parameters passed on the stack
//          (overflow args beyond x0-x7). Regression test for BUG-NAT-002
//          where hardcoded physical registers for stack param loading
//          conflicted with the register allocator.
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

/// @brief Sum of 10 parameters (2 on stack) returned as exit code.
/// Tests basic callee-side stack parameter loading.
TEST(Arm64CLI, CalleeStackParamsSum10)
{
    const std::string in = outPath("arm64_callee_stack_params_sum10.il");
    const std::string il =
        "il 0.1\n"
        "func @sum10(%a:i64, %b:i64, %c:i64, %d:i64, %e:i64, %f:i64, "
        "%g:i64, %h:i64, %i:i64, %j:i64) -> i64 {\n"
        "entry(%a:i64, %b:i64, %c:i64, %d:i64, %e:i64, %f:i64, "
        "%g:i64, %h:i64, %i:i64, %j:i64):\n"
        "  %t1 = add %a, %b\n"
        "  %t2 = add %t1, %c\n"
        "  %t3 = add %t2, %d\n"
        "  %t4 = add %t3, %e\n"
        "  %t5 = add %t4, %f\n"
        "  %t6 = add %t5, %g\n"
        "  %t7 = add %t6, %h\n"
        "  %t8 = add %t7, %i\n"
        "  %t9 = add %t8, %j\n"
        "  ret %t9\n"
        "}\n"
        "func @main() -> i64 {\n"
        "entry:\n"
        "  %r = call @sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10)\n"
        "  ret %r\n"
        "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    // 1+2+3+4+5+6+7+8+9+10 = 55
    ASSERT_EQ(rc, 55);
}

/// @brief Uses stack params after a function call that clobbers caller-saved
/// registers. This is the exact scenario that triggered BUG-NAT-002: the
/// register allocator assigned a callee's vreg to the same physical register
/// (X10) that was hardcoded for stack param loading in the prologue.
TEST(Arm64CLI, CalleeStackParamsSurviveCall)
{
    const std::string in = outPath("arm64_callee_stack_params_survive.il");
    const std::string il =
        "il 0.1\n"
        // Helper that returns its argument (forces a call that clobbers regs)
        "func @identity(%x:i64) -> i64 {\n"
        "entry(%x:i64):\n"
        "  ret %x\n"
        "}\n"
        // Callee: 10 params, uses stack params %i and %j after calling identity
        "func @use_after_call(%a:i64, %b:i64, %c:i64, %d:i64, %e:i64, "
        "%f:i64, %g:i64, %h:i64, %i:i64, %j:i64) -> i64 {\n"
        "entry(%a:i64, %b:i64, %c:i64, %d:i64, %e:i64, %f:i64, "
        "%g:i64, %h:i64, %i:i64, %j:i64):\n"
        "  %dummy = call @identity(%a)\n"
        "  %sum = add %i, %j\n"
        "  ret %sum\n"
        "}\n"
        "func @main() -> i64 {\n"
        "entry:\n"
        "  %r = call @use_after_call(1, 2, 3, 4, 5, 6, 7, 8, 9, 10)\n"
        "  ret %r\n"
        "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    // %i=9, %j=10, sum=19
    ASSERT_EQ(rc, 19);
}

/// @brief 13 parameters (5 on stack) with all register params used alongside
/// stack params. Tests that register allocator doesn't conflict with stack
/// param loading even under high register pressure.
TEST(Arm64CLI, CalleeStackParams13Wide)
{
    const std::string in = outPath("arm64_callee_stack_params_13wide.il");
    const std::string il =
        "il 0.1\n"
        "func @identity(%x:i64) -> i64 {\n"
        "entry(%x:i64):\n"
        "  ret %x\n"
        "}\n"
        "func @wide13(%p0:i64, %p1:i64, %p2:i64, %p3:i64, %p4:i64, "
        "%p5:i64, %p6:i64, %p7:i64, %p8:i64, %p9:i64, %p10:i64, "
        "%p11:i64, %p12:i64) -> i64 {\n"
        "entry(%p0:i64, %p1:i64, %p2:i64, %p3:i64, %p4:i64, "
        "%p5:i64, %p6:i64, %p7:i64, %p8:i64, %p9:i64, %p10:i64, "
        "%p11:i64, %p12:i64):\n"
        // Use register params to keep them live
        "  %s1 = add %p0, %p1\n"
        "  %s2 = add %s1, %p2\n"
        // Call to clobber caller-saved regs
        "  %dummy = call @identity(%s2)\n"
        // Use remaining register params and all stack params after the call
        "  %s3 = add %p3, %p4\n"
        "  %s4 = add %s3, %p5\n"
        "  %s5 = add %s4, %p6\n"
        "  %s6 = add %s5, %p7\n"
        // Stack params (p8-p12)
        "  %s7 = add %s6, %p8\n"
        "  %s8 = add %s7, %p9\n"
        "  %s9 = add %s8, %p10\n"
        "  %s10 = add %s9, %p11\n"
        "  %s11 = add %s10, %p12\n"
        "  ret %s11\n"
        "}\n"
        "func @main() -> i64 {\n"
        "entry:\n"
        "  %r = call @wide13(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13)\n"
        "  ret %r\n"
        "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    // p3+p4+p5+p6+p7+p8+p9+p10+p11+p12 = 4+5+6+7+8+9+10+11+12+13 = 85
    // (p0+p1+p2 are computed and passed to identity() but not chained into the final sum)
    ASSERT_EQ(rc, 85);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
