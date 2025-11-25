//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_array_access.cpp
// Purpose: Verify AArch64 lowers array-like access patterns: base + index*8.
//
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

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

// Test: load from base[idx] where element size is 8
TEST(Arm64CLI, ArrayAccess_LoadIndex)
{
    const std::string in = outPath("arm64_array_load_idx.il");
    const std::string out = outPath("arm64_array_load_idx.s");
    const std::string il = "il 0.1\n"
                           "func @load_idx(%base:ptr, %idx:i64) -> i64 {\n"
                           "entry(%base:ptr, %idx:i64):\n"
                           "  %scaled = shl %idx, 3\n"
                           "  %p = gep %base, %scaled\n"
                           "  %v = load i64, %p\n"
                           "  ret %v\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect: lsl for scaling, add for pointer arithmetic, ldr from computed address
    EXPECT_NE(asmText.find("lsl "), std::string::npos);
    EXPECT_NE(asmText.find(" add "), std::string::npos);
    EXPECT_NE(asmText.find("ldr x"), std::string::npos);
    EXPECT_NE(asmText.find("[x"), std::string::npos);
}

// Test: store to base[idx] where element size is 8
TEST(Arm64CLI, ArrayAccess_StoreIndex)
{
    const std::string in = outPath("arm64_array_store_idx.il");
    const std::string out = outPath("arm64_array_store_idx.s");
    const std::string il = "il 0.1\n"
                           "func @store_idx(%base:ptr, %idx:i64, %val:i64) -> i64 {\n"
                           "entry(%base:ptr, %idx:i64, %val:i64):\n"
                           "  %scaled = shl %idx, 3\n"
                           "  %p = gep %base, %scaled\n"
                           "  store i64, %p, %val\n"
                           "  ret %val\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect: lsl for scaling, add for pointer arithmetic, str to computed address
    EXPECT_NE(asmText.find("lsl "), std::string::npos);
    EXPECT_NE(asmText.find(" add "), std::string::npos);
    EXPECT_NE(asmText.find("str x"), std::string::npos);
    EXPECT_NE(asmText.find("[x"), std::string::npos);
}

// Test: load from base with constant offset (field access)
TEST(Arm64CLI, ArrayAccess_ConstOffset)
{
    const std::string in = outPath("arm64_const_offset.il");
    const std::string out = outPath("arm64_const_offset.s");
    const std::string il = "il 0.1\n"
                           "func @load_field(%obj:ptr) -> i64 {\n"
                           "entry(%obj:ptr):\n"
                           "  %p = gep %obj, 16\n"
                           "  %v = load i64, %p\n"
                           "  ret %v\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect: add with immediate for constant offset
    EXPECT_NE(asmText.find("add x"), std::string::npos);
    EXPECT_NE(asmText.find("#16"), std::string::npos);
    EXPECT_NE(asmText.find("ldr x"), std::string::npos);
}

// Test: combined array element plus field offset (struct in array)
TEST(Arm64CLI, ArrayAccess_StructInArray)
{
    const std::string in = outPath("arm64_struct_in_array.il");
    const std::string out = outPath("arm64_struct_in_array.s");
    // base[idx].field where struct size=24, field offset=8
    const std::string il = "il 0.1\n"
                           "func @load_struct_field(%base:ptr, %idx:i64) -> i64 {\n"
                           "entry(%base:ptr, %idx:i64):\n"
                           "  %struct_size = mul %idx, 24\n"
                           "  %elem_ptr = gep %base, %struct_size\n"
                           "  %field_ptr = gep %elem_ptr, 8\n"
                           "  %v = load i64, %field_ptr\n"
                           "  ret %v\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect: mul for struct size scaling, multiple adds
    EXPECT_NE(asmText.find("mul "), std::string::npos);
    EXPECT_NE(asmText.find(" add "), std::string::npos);
    EXPECT_NE(asmText.find("ldr x"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
