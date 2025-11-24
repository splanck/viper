//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_stack_locals_cli.cpp
// Purpose: Verify AArch64 CLI (-S) handles stack locals (alloca/load/store).
// Key invariants: Emits FP-relative str/ldr and adjusts sp for locals.
// Ownership/Lifetime: Test allocates temporary files under build/test-out/arm64.
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

TEST(Arm64CLI, StackLocals_AllocaStoreLoad)
{
    const std::string in = "arm64_cli_stack_locals.il";
    const std::string out = "arm64_cli_stack_locals.s";
    // Minimal IL: one i64 param, alloca 8, store param into it, load back, return
    const std::string il =
        "il 0.1\n"
        "func @test_local(%a:i64) -> i64 {\n"
        "entry(%a:i64):\n"
        "  %t0 = alloca 8\n"
        "  store i64, %t0, %a\n"
        "  %t1 = load i64, %t0\n"
        "  ret %t1\n"
        "}\n";

    const std::string inP = outPath(in);
    const std::string outP = outPath(out);
    writeFile(inP, il);

    const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
    const int rc = cmd_codegen_arm64(3, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);

    const std::string asmText = readFile(outP);
    // Prologue and frame pointer set
    EXPECT_NE(asmText.find("stp x29, x30"), std::string::npos);
    EXPECT_NE(asmText.find("mov x29, sp"), std::string::npos);
    // Stack allocation for locals
    EXPECT_NE(asmText.find("sub sp, sp, #"), std::string::npos);
    // Store/load via FP-relative addressing to the same frame area
    EXPECT_NE(asmText.find("str x"), std::string::npos);
    EXPECT_NE(asmText.find("[x29, #"), std::string::npos);
    EXPECT_NE(asmText.find("ldr x"), std::string::npos);
    // Epilogue restores SP and returns
    EXPECT_NE(asmText.find("add sp, sp, #"), std::string::npos);
    EXPECT_NE(asmText.find("ldp x29, x30"), std::string::npos);
    EXPECT_NE(asmText.find("ret"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}

