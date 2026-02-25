//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_leaf_func.cpp
// Purpose: Verify leaf function optimization — functions with no calls skip
//          the FP/LR save/restore prologue and epilogue.
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

/// A simple leaf function (no calls) should NOT have stp x29, x30 in its body.
/// Note: @main always has runtime init calls, so we test a non-main function.
TEST(Arm64LeafFunc, LeafFunctionSkipsPrologue)
{
    const std::string il = "il 0.1.2\n"
                           "func @leaf(%x:i64, %y:i64) -> i64 {\n"
                           "entry:\n"
                           "  %r = iadd.ovf %x, %y\n"
                           "  ret %r\n"
                           "}\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  ret 0\n"
                           "}\n";

    const std::string inP = outPath("arm64_leaf_func.il");
    const std::string outP = outPath("arm64_leaf_func.s");
    writeFile(inP, il);

    const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
    const int rc = cmd_codegen_arm64(3, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);

    const std::string asmText = readFile(outP);

    // Find the leaf function's assembly
    auto leafPos = asmText.find("_leaf:");
    if (leafPos == std::string::npos)
        leafPos = asmText.find("leaf:");
    ASSERT_NE(leafPos, std::string::npos);

    // Find the main function (comes after leaf)
    auto mainPos = asmText.find("_main:", leafPos);
    if (mainPos == std::string::npos)
        mainPos = asmText.find("main:", leafPos);
    ASSERT_NE(mainPos, std::string::npos);

    // Extract just the leaf function's assembly
    std::string leafAsm = asmText.substr(leafPos, mainPos - leafPos);

    // Leaf function should NOT have stp x29, x30 (frame pointer save)
    EXPECT_EQ(leafAsm.find("stp x29, x30"), std::string::npos);
    // Leaf function should NOT have ldp x29, x30 (frame pointer restore)
    EXPECT_EQ(leafAsm.find("ldp x29, x30"), std::string::npos);
    // But it should still have ret
    EXPECT_NE(leafAsm.find("ret"), std::string::npos);
}

/// A non-leaf function (with calls) should still have the full prologue.
TEST(Arm64LeafFunc, NonLeafFunctionHasPrologue)
{
    const std::string il = "il 0.1.2\n"
                           "func @helper() -> i64 {\n"
                           "entry:\n"
                           "  ret 1\n"
                           "}\n"
                           "func @caller() -> i64 {\n"
                           "entry:\n"
                           "  %r = call @helper()\n"
                           "  ret %r\n"
                           "}\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  ret 0\n"
                           "}\n";

    const std::string inP = outPath("arm64_nonleaf_func.il");
    const std::string outP = outPath("arm64_nonleaf_func.s");
    writeFile(inP, il);

    const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
    const int rc = cmd_codegen_arm64(3, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);

    const std::string asmText = readFile(outP);

    // Find the caller function's assembly
    auto callerPos = asmText.find("_caller:");
    if (callerPos == std::string::npos)
        callerPos = asmText.find("caller:");
    ASSERT_NE(callerPos, std::string::npos);

    // Find the next function after caller
    auto mainPos = asmText.find("_main:", callerPos);
    if (mainPos == std::string::npos)
        mainPos = asmText.find("main:", callerPos);
    ASSERT_NE(mainPos, std::string::npos);

    std::string callerAsm = asmText.substr(callerPos, mainPos - callerPos);

    // Non-leaf function SHOULD have stp x29, x30
    EXPECT_NE(callerAsm.find("stp x29, x30"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
