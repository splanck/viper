//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_idxchk.cpp
// Purpose: Verify index bounds checking (idxchk) lowering on AArch64.
// Key invariants: Generates compare + conditional trap for out-of-bounds.
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

// Test 1: Simple index check - compare index against bounds
TEST(Arm64IdxChk, SimpleCheck)
{
    const std::string in = outPath("arm64_idxchk_simple.il");
    const std::string out = outPath("arm64_idxchk_simple.s");
    // idx.chk checks that lo <= idx < hi
    const std::string il = "il 0.1\n"
                           "func @f(%idx:i64, %len:i64) -> i64 {\n"
                           "entry(%idx:i64, %len:i64):\n"
                           "  %checked = idx.chk %idx, 0, %len\n"
                           "  ret %checked\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have a compare instruction
    EXPECT_NE(asmText.find("cmp x"), std::string::npos);
}

// Test 2: Index check with immediate bounds
TEST(Arm64IdxChk, ImmediateBounds)
{
    const std::string in = outPath("arm64_idxchk_imm.il");
    const std::string out = outPath("arm64_idxchk_imm.s");
    const std::string il = "il 0.1\n"
                           "func @f(%idx:i64) -> i64 {\n"
                           "entry(%idx:i64):\n"
                           "  %checked = idx.chk %idx, 0, 10\n"
                           "  ret %checked\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have a compare instruction
    EXPECT_NE(asmText.find("cmp x"), std::string::npos);
}

// Test 3: Multiple index checks in sequence
TEST(Arm64IdxChk, MultipleChecks)
{
    const std::string in = outPath("arm64_idxchk_multi.il");
    const std::string out = outPath("arm64_idxchk_multi.s");
    const std::string il = "il 0.1\n"
                           "func @f(%i1:i64, %i2:i64, %len:i64) -> i64 {\n"
                           "entry(%i1:i64, %i2:i64, %len:i64):\n"
                           "  %c1 = idx.chk %i1, 0, %len\n"
                           "  %c2 = idx.chk %i2, 0, %len\n"
                           "  %sum = add %c1, %c2\n"
                           "  ret %sum\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Should have multiple compare instructions
    std::size_t cmpCount = 0;
    std::size_t pos = 0;
    while ((pos = asmText.find("cmp x", pos)) != std::string::npos)
    {
        ++cmpCount;
        pos += 5;
    }
    EXPECT_TRUE(cmpCount >= 2U);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
