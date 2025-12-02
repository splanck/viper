//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_call_args.cpp
// Purpose: Verify CLI marshals params/consts into x0..x7 before `bl`.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
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

/// @brief Returns the expected mangled symbol name for a call target.
static std::string blSym(const std::string &name)
{
#if defined(__APPLE__)
    return "bl _" + name;
#else
    return "bl " + name;
#endif
}

TEST(Arm64CLI, CallRI_MarshalImm)
{
    const std::string in = outPath("arm64_call_ri.il");
    const std::string out = outPath("arm64_call_ri.s");
    const std::string il = "il 0.1\n"
                           "extern @h(i64, i64) -> i64\n"
                           "func @f(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %t0 = call @h(%a, 5)\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("mov x1, #5"), std::string::npos);
    EXPECT_NE(asmText.find(blSym("h")), std::string::npos);
}

TEST(Arm64CLI, CallRR_Swap)
{
    const std::string in = outPath("arm64_call_swap.il");
    const std::string out = outPath("arm64_call_swap.s");
    const std::string il = "il 0.1\n"
                           "extern @h(i64, i64) -> i64\n"
                           "func @f(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %t0 = call @h(%b, %a)\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect use of scratch (x9) or direct swap; accept either form.
    EXPECT_NE(asmText.find(blSym("h")), std::string::npos);
    const bool direct = (asmText.find("mov x0, x1") != std::string::npos) &&
                        (asmText.find("mov x1, x0") != std::string::npos);
    const bool scratch = (asmText.find("mov x9, x1") != std::string::npos) &&
                         (asmText.find("mov x0, x9") != std::string::npos) &&
                         (asmText.find("mov x1, x0") != std::string::npos);
    EXPECT_TRUE(direct || scratch);
}

TEST(Arm64CLI, CallRRI_ThreeArgs)
{
    const std::string in = outPath("arm64_call_three.il");
    const std::string out = outPath("arm64_call_three.s");
    const std::string il = "il 0.1\n"
                           "extern @h(i64, i64, i64) -> i64\n"
                           "func @f(%a:i64, %b:i64, %c:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64, %c:i64):\n"
                           "  %t0 = call @h(%b, 7, %a)\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("mov x0, x1"), std::string::npos);
    EXPECT_NE(asmText.find("mov x1, #7"), std::string::npos);
    EXPECT_NE(asmText.find("mov x2, x0"), std::string::npos);
    EXPECT_NE(asmText.find(blSym("h")), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
