//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_string_store_refcount.cpp
// Purpose: Verify string store operations with reference counting on AArch64.
// Key invariants: String stores call runtime helpers for refcount management.
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

// Test 1: Simple string store - uses store str, ptr, value
TEST(Arm64StringStore, SimpleStore)
{
    const std::string in = outPath("arm64_str_store_simple.il");
    const std::string out = outPath("arm64_str_store_simple.s");
    const std::string il = "il 0.1\n"
                           "func @store_str(%dst:ptr, %src:str) -> i64 {\n"
                           "entry(%dst:ptr, %src:str):\n"
                           "  store str, %dst, %src\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // String store should generate str instruction
    EXPECT_NE(asmText.find("str x"), std::string::npos);
}

// Test 2: String retain
TEST(Arm64StringStore, StringRetain)
{
    const std::string in = outPath("arm64_str_retain.il");
    const std::string out = outPath("arm64_str_retain.s");
    const std::string il = "il 0.1\n"
                           "extern @rt_str_retain(str) -> str\n"
                           "func @retain(%s:str) -> str {\n"
                           "entry(%s:str):\n"
                           "  %r = call @rt_str_retain(%s)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have call to rt_str_retain
    EXPECT_NE(asmText.find(blSym("rt_str_retain")), std::string::npos);
}

// Test 3: String release
TEST(Arm64StringStore, StringRelease)
{
    const std::string in = outPath("arm64_str_release.il");
    const std::string out = outPath("arm64_str_release.s");
    const std::string il = "il 0.1\n"
                           "extern @rt_str_release(str) -> void\n"
                           "func @release(%s:str) -> i64 {\n"
                           "entry(%s:str):\n"
                           "  call @rt_str_release(%s)\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have call to rt_str_release
    EXPECT_NE(asmText.find(blSym("rt_str_release")), std::string::npos);
}

// Test 4: String concatenation via runtime
TEST(Arm64StringStore, StringConcat)
{
    const std::string in = outPath("arm64_str_concat.il");
    const std::string out = outPath("arm64_str_concat.s");
    const std::string il = "il 0.1\n"
                           "extern @rt_str_concat(str, str) -> str\n"
                           "func @concat(%a:str, %b:str) -> str {\n"
                           "entry(%a:str, %b:str):\n"
                           "  %r = call @rt_str_concat(%a, %b)\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have call to rt_str_concat
    EXPECT_NE(asmText.find(blSym("rt_str_concat")), std::string::npos);
}

// Test 5: String field access via gep offset (simplified - no user types)
TEST(Arm64StringStore, LoadStoreField)
{
    const std::string in = outPath("arm64_str_field.il");
    const std::string out = outPath("arm64_str_field.s");
    // Test string field access using gep with byte offset (8 bytes for second field)
    const std::string il = "il 0.1\n"
                           "func @copy_field(%obj:ptr, %newval:str) -> i64 {\n"
                           "entry(%obj:ptr, %newval:str):\n"
                           "  %fieldptr = gep %obj, 8\n"
                           "  store str, %fieldptr, %newval\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Code should compile
    EXPECT_FALSE(asmText.empty());
}

// Test 6: String in array
TEST(Arm64StringStore, StringArray)
{
    const std::string in = outPath("arm64_str_array.il");
    const std::string out = outPath("arm64_str_array.s");
    const std::string il = "il 0.1\n"
                           "extern @rt_arr_str_put(ptr, i64, str) -> void\n"
                           "func @put_str(%arr:ptr, %idx:i64, %val:str) -> i64 {\n"
                           "entry(%arr:ptr, %idx:i64, %val:str):\n"
                           "  call @rt_arr_str_put(%arr, %idx, %val)\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find(blSym("rt_arr_str_put")), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
