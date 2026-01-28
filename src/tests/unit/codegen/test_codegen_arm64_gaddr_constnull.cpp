//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_gaddr_constnull.cpp
// Purpose: Verify global address materialization and const.null on AArch64.
// Key invariants: Global addresses use adrp+add, null is xzr or mov #0.
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

// Test 1: Global string address (gaddr produces ptr to global)
TEST(Arm64GaddrNull, GlobalAddress)
{
    const std::string in = outPath("arm64_gaddr.il");
    const std::string out = outPath("arm64_gaddr.s");
    // Use string global since parser currently only supports str type
    const std::string il = "il 0.1\n"
                           "global const str @gvar = \"test\"\n"
                           "func @get_addr() -> ptr {\n"
                           "entry:\n"
                           "  %p = gaddr @gvar\n"
                           "  ret %p\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect adrp for page address
    EXPECT_NE(asmText.find("adrp x"), std::string::npos);
    // Expect add for page offset (Mach-O style)
    EXPECT_NE(asmText.find("add x"), std::string::npos);
}

// Test 2: const_null returns null pointer
TEST(Arm64GaddrNull, ConstNull)
{
    const std::string in = outPath("arm64_constnull.il");
    const std::string out = outPath("arm64_constnull.s");
    const std::string il = "il 0.1\n"
                           "func @get_null() -> ptr {\n"
                           "entry:\n"
                           "  %p = const_null\n"
                           "  ret %p\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // The const_null loads 0 into a register - could be mov xN, #0 pattern
    bool hasNull = asmText.find("mov x0, #0") != std::string::npos ||
                   asmText.find("mov x0, xzr") != std::string::npos ||
                   asmText.find("mov x0, 0") != std::string::npos ||
                   asmText.find(", #0") != std::string::npos; // Any mov of #0
    EXPECT_TRUE(hasNull);
}

// Test 3: Load from pointer (via alloca since integer globals not yet supported)
TEST(Arm64GaddrNull, LoadFromPointer)
{
    const std::string in = outPath("arm64_load_ptr.il");
    const std::string out = outPath("arm64_load_ptr.s");
    const std::string il = "il 0.1\n"
                           "func @load_value() -> i64 {\n"
                           "entry:\n"
                           "  %p = alloca 8\n"
                           "  store i64, %p, 42\n"
                           "  %v = load i64, %p\n"
                           "  ret %v\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect ldr for load
    EXPECT_NE(asmText.find("ldr x"), std::string::npos);
}

// Test 4: Store to pointer
TEST(Arm64GaddrNull, StoreToPointer)
{
    const std::string in = outPath("arm64_store_ptr.il");
    const std::string out = outPath("arm64_store_ptr.s");
    const std::string il = "il 0.1\n"
                           "func @store_value(%v:i64) -> i64 {\n"
                           "entry(%v:i64):\n"
                           "  %p = alloca 8\n"
                           "  store i64, %p, %v\n"
                           "  %r = load i64, %p\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect str for store
    EXPECT_NE(asmText.find("str x"), std::string::npos);
}

// Test 5: Compare pointer with null
TEST(Arm64GaddrNull, CmpWithNull)
{
    const std::string in = outPath("arm64_cmp_null.il");
    const std::string out = outPath("arm64_cmp_null.s");
    const std::string il = "il 0.1\n"
                           "func @is_null(%p:ptr) -> i64 {\n"
                           "entry(%p:ptr):\n"
                           "  %n = const_null\n"
                           "  %c = icmp_eq %p, %n\n"
                           "  %r = zext1 %c\n"
                           "  ret %r\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have compare (could be cmp with 0 or cbz pattern)
    bool hasCmp = asmText.find("cmp x") != std::string::npos ||
                  asmText.find("cbz x") != std::string::npos ||
                  asmText.find("cbnz x") != std::string::npos;
    EXPECT_TRUE(hasCmp);
}

// Test 6: Multiple string globals
TEST(Arm64GaddrNull, MultipleGlobals)
{
    const std::string in = outPath("arm64_multi_global.il");
    const std::string out = outPath("arm64_multi_global.s");
    const std::string il = "il 0.1\n"
                           "global const str @a = \"hello\"\n"
                           "global const str @b = \"world\"\n"
                           "func @get_addrs() -> ptr {\n"
                           "entry:\n"
                           "  %pa = gaddr @a\n"
                           "  %pb = gaddr @b\n"
                           "  ret %pa\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have multiple adrp
    std::size_t adrpCount = 0;
    std::size_t pos = 0;
    while ((pos = asmText.find("adrp x", pos)) != std::string::npos)
    {
        ++adrpCount;
        pos += 6;
    }
    EXPECT_TRUE(adrpCount >= 2U);
}

// Test 7: String constant address
TEST(Arm64GaddrNull, StringConstant)
{
    const std::string in = outPath("arm64_str_const.il");
    const std::string out = outPath("arm64_str_const.s");
    const std::string il = "il 0.1\n"
                           "global const str @greeting = \"hello\"\n"
                           "func @get_greeting() -> str {\n"
                           "entry:\n"
                           "  %s = const_str @greeting\n"
                           "  ret %s\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have string data section or reference
    EXPECT_FALSE(asmText.empty());
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
