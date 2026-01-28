//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_abi_mixed_args.cpp
// Purpose: Verify ABI compliance for mixed integer and floating-point arguments.
// Key invariants: ints go to x0-x7, floats go to d0-d7 independently.
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

// Test 1: Simple mixed args - one int, one float
TEST(Arm64ABIMixed, OneIntOneFloat)
{
    const std::string in = outPath("arm64_abi_mix1.il");
    const std::string out = outPath("arm64_abi_mix1.s");
    const std::string il = "il 0.1\n"
                           "extern @mixed(i64, f64) -> f64\n"
                           "func @caller(%n:i64, %x:f64) -> f64 {\n"
                           "entry(%n:i64, %x:f64):\n"
                           "  %r = call @mixed(%n, %x)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find(blSym("mixed")), std::string::npos);
}

// Test 2: Interleaved int and float args
TEST(Arm64ABIMixed, InterleavedArgs)
{
    const std::string in = outPath("arm64_abi_mix2.il");
    const std::string out = outPath("arm64_abi_mix2.s");
    const std::string il = "il 0.1\n"
                           "extern @interleaved(i64, f64, i64, f64) -> f64\n"
                           "func @caller(%a:i64, %x:f64, %b:i64, %y:f64) -> f64 {\n"
                           "entry(%a:i64, %x:f64, %b:i64, %y:f64):\n"
                           "  %r = call @interleaved(%a, %x, %b, %y)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find(blSym("interleaved")), std::string::npos);
}

// Test 3: Many integers, one float
TEST(Arm64ABIMixed, ManyIntsOneFloat)
{
    const std::string in = outPath("arm64_abi_many_int.il");
    const std::string out = outPath("arm64_abi_many_int.s");
    const std::string il = "il 0.1\n"
                           "extern @many_int(i64, i64, i64, i64, f64) -> i64\n"
                           "func @caller(%a:i64, %b:i64, %c:i64, %d:i64, %x:f64) -> i64 {\n"
                           "entry(%a:i64, %b:i64, %c:i64, %d:i64, %x:f64):\n"
                           "  %r = call @many_int(%a, %b, %c, %d, %x)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find(blSym("many_int")), std::string::npos);
}

// Test 4: Many floats, one int
TEST(Arm64ABIMixed, ManyFloatsOneInt)
{
    const std::string in = outPath("arm64_abi_many_fp.il");
    const std::string out = outPath("arm64_abi_many_fp.s");
    const std::string il = "il 0.1\n"
                           "extern @many_fp(f64, f64, f64, f64, i64) -> f64\n"
                           "func @caller(%a:f64, %b:f64, %c:f64, %d:f64, %n:i64) -> f64 {\n"
                           "entry(%a:f64, %b:f64, %c:f64, %d:f64, %n:i64):\n"
                           "  %r = call @many_fp(%a, %b, %c, %d, %n)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find(blSym("many_fp")), std::string::npos);
}

// Test 5: All 8 int registers filled, plus floats
TEST(Arm64ABIMixed, MaxIntsWithFloats)
{
    const std::string in = outPath("arm64_abi_max_int.il");
    const std::string out = outPath("arm64_abi_max_int.s");
    const std::string il =
        "il 0.1\n"
        "extern @max_int(i64, i64, i64, i64, i64, i64, i64, i64, f64, f64) -> i64\n"
        "func @caller(%a:i64, %b:i64, %c:i64, %d:i64, %e:i64, %f:i64, %g:i64, %h:i64, %x:f64, "
        "%y:f64) -> i64 {\n"
        "entry(%a:i64, %b:i64, %c:i64, %d:i64, %e:i64, %f:i64, %g:i64, %h:i64, %x:f64, %y:f64):\n"
        "  %r = call @max_int(%a, %b, %c, %d, %e, %f, %g, %h, %x, %y)\n"
        "  ret %r\n"
        "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find(blSym("max_int")), std::string::npos);
}

// Test 6: All 8 float registers filled, plus ints
TEST(Arm64ABIMixed, MaxFloatsWithInts)
{
    const std::string in = outPath("arm64_abi_max_fp.il");
    const std::string out = outPath("arm64_abi_max_fp.s");
    const std::string il =
        "il 0.1\n"
        "extern @max_fp(f64, f64, f64, f64, f64, f64, f64, f64, i64, i64) -> f64\n"
        "func @caller(%a:f64, %b:f64, %c:f64, %d:f64, %e:f64, %f:f64, %g:f64, %h:f64, %x:i64, "
        "%y:i64) -> f64 {\n"
        "entry(%a:f64, %b:f64, %c:f64, %d:f64, %e:f64, %f:f64, %g:f64, %h:f64, %x:i64, %y:i64):\n"
        "  %r = call @max_fp(%a, %b, %c, %d, %e, %f, %g, %h, %x, %y)\n"
        "  ret %r\n"
        "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find(blSym("max_fp")), std::string::npos);
}

// Test 7: Overflow to stack with mixed args
TEST(Arm64ABIMixed, StackOverflowMixed)
{
    const std::string in = outPath("arm64_abi_stack_mix.il");
    const std::string out = outPath("arm64_abi_stack_mix.s");
    // 9 ints (one goes to stack) + 9 floats (one goes to stack)
    const std::string il =
        "il 0.1\n"
        "extern @stack_mix(i64, i64, i64, i64, i64, i64, i64, i64, i64, f64, f64, f64, f64, f64, "
        "f64, f64, f64, f64) -> i64\n"
        "func @caller(%i1:i64, %i2:i64, %i3:i64, %i4:i64, %i5:i64, %i6:i64, %i7:i64, %i8:i64, "
        "%i9:i64, "
        "%f1:f64, %f2:f64, %f3:f64, %f4:f64, %f5:f64, %f6:f64, %f7:f64, %f8:f64, %f9:f64) -> i64 "
        "{\n"
        "entry(%i1:i64, %i2:i64, %i3:i64, %i4:i64, %i5:i64, %i6:i64, %i7:i64, %i8:i64, %i9:i64, "
        "%f1:f64, %f2:f64, %f3:f64, %f4:f64, %f5:f64, %f6:f64, %f7:f64, %f8:f64, %f9:f64):\n"
        "  %r = call @stack_mix(%i1, %i2, %i3, %i4, %i5, %i6, %i7, %i8, %i9, "
        "%f1, %f2, %f3, %f4, %f5, %f6, %f7, %f8, %f9)\n"
        "  ret %r\n"
        "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have str for stack args
    bool hasStackStore =
        asmText.find("str x") != std::string::npos || asmText.find("str d") != std::string::npos;
    EXPECT_TRUE(hasStackStore);
}

// Test 8: Return int, receive float
TEST(Arm64ABIMixed, ReturnIntReceiveFloat)
{
    const std::string in = outPath("arm64_abi_ret_int.il");
    const std::string out = outPath("arm64_abi_ret_int.s");
    const std::string il = "il 0.1\n"
                           "extern @to_int(f64) -> i64\n"
                           "func @caller(%x:f64) -> i64 {\n"
                           "entry(%x:f64):\n"
                           "  %r = call @to_int(%x)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find(blSym("to_int")), std::string::npos);
}

// Test 9: Return float, receive int
TEST(Arm64ABIMixed, ReturnFloatReceiveInt)
{
    const std::string in = outPath("arm64_abi_ret_fp.il");
    const std::string out = outPath("arm64_abi_ret_fp.s");
    const std::string il = "il 0.1\n"
                           "extern @to_float(i64) -> f64\n"
                           "func @caller(%n:i64) -> f64 {\n"
                           "entry(%n:i64):\n"
                           "  %r = call @to_float(%n)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find(blSym("to_float")), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
