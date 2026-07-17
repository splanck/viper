//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_add_params.cpp
// Purpose: Verify arm64 CLI lowers simple add of two entry parameters.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "tools/zanna/cmd_codegen_arm64.hpp"

using namespace zanna::tools::ilc;

static std::string outPath(const std::string &name) {
    namespace fs = std::filesystem;
    const fs::path dir{"build/test-out/arm64"};
    fs::create_directories(dir);
    return (dir / name).string();
}

static void writeFile(const std::string &path, const std::string &text) {
    std::ofstream ofs(path);
    ASSERT_TRUE(static_cast<bool>(ofs));
    ofs << text;
}

static std::string readFile(const std::string &path) {
    std::ifstream ifs(path);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

TEST(Arm64CLI, AddTwoParams) {
    const std::string in = "arm64_cli_add2.il";
    const std::string out = "arm64_cli_add2.s";
    const std::string il = "il 0.1\n"
                           "func @add2(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %t0 = iadd.ovf %a, %b\n"
                           "  ret %t0\n"
                           "}\n";
    const std::string inP = outPath(in);
    const std::string outP = outPath(out);
    writeFile(inP, il);

    const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
    const int rc = cmd_codegen_arm64(3, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);
    const std::string asmText = readFile(outP);
    // Expect checked-add lowering. Regalloc may compute into any physical GPR
    // and then move the result into x0 for the ABI return value.
    EXPECT_NE(asmText.find("adds x"), std::string::npos);
    EXPECT_NE(asmText.find("b.vs"), std::string::npos);
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, &argv);
    return zanna_test::run_all_tests();
}
