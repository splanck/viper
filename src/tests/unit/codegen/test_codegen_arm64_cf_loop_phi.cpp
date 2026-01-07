//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_cf_loop_phi.cpp
// Purpose: Verify loop lowering with loop-carried block params.
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
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

TEST(Arm64CLI, CF_Loop_Phi)
{
    const std::string in = outPath("arm64_cf_loop.il");
    const std::string out = outPath("arm64_cf_loop.s");
    // Sum 1..10 using loop-carried phi
    const std::string il = "il 0.1\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  br loop(0, 0)\n"
                           "loop(%i:i64, %acc:i64):\n"
                           "  %c = scmp_ge %i, 10\n"
                           "  cbr %c, exit(%acc), body(%i, %acc)\n"
                           "body(%i:i64, %acc:i64):\n"
                           "  %i1 = iadd.ovf %i, 1\n"
                           "  %acc1 = iadd.ovf %acc, %i1\n"
                           "  br loop(%i1, %acc1)\n"
                           "exit(%res:i64):\n"
                           "  ret %res\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect register moves implementing phi and branches; no edge labels.
    // Block parameters now use spill slots for correctness across block boundaries.
    EXPECT_EQ(asmText.find(".edge.t."), std::string::npos);
    EXPECT_EQ(asmText.find(".edge.f."), std::string::npos);
    // Phi values passed via spill slots - stores and loads expected
    EXPECT_NE(asmText.find(" str x"), std::string::npos);
    EXPECT_NE(asmText.find(" ldr x"), std::string::npos);
    EXPECT_NE(asmText.find(" mov x"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
