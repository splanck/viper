//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_cf_if_else_phi.cpp
// Purpose: Verify if/else lowering with block params (phi via edge transport).
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "tools/viper/cmd_codegen_arm64.hpp"

using namespace viper::tools::ilc;

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

TEST(Arm64CLI, CF_IfElse_Phi) {
    const std::string in = outPath("arm64_cf_ifelse.il");
    const std::string out = outPath("arm64_cf_ifelse.s");
    const std::string il = "il 0.2.0\n"
                           "func @f(%x:i64) -> i64 {\n"
                           "entry(%x:i64):\n"
                           "  %cond = scmp_gt %x, 0\n"
                           "  cbr %cond, then, else\n"
                           "then:\n"
                           "  br join(1)\n"
                           "else:\n"
                           "  br join(2)\n"
                           "join(%v:i64):\n"
                           "  ret %v\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect conditional branch present. Block parameters may travel via spill
    // slots or direct edge copies depending on the join simplification pass.
    EXPECT_NE(asmText.find("b."), std::string::npos);
    EXPECT_EQ(asmText.find(".edge.t."), std::string::npos);
    EXPECT_EQ(asmText.find(".edge.f."), std::string::npos);
    EXPECT_NE(asmText.find("Ljoin:"), std::string::npos);
    EXPECT_NE(asmText.find(" mov x0, x"), std::string::npos);
    const bool hasSpillTransport =
        asmText.find(" str x") != std::string::npos || asmText.find(" ldr x") != std::string::npos;
    const bool hasEdgeSplit = asmText.find(".Ledge_") != std::string::npos;
    EXPECT_TRUE(hasSpillTransport || hasEdgeSplit);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
