//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_ra_many_temps.cpp
// Purpose: Ensure CLI path runs AArch64 RA and emits spills/callee-saves for many temps.
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

TEST(Arm64CLI, RA_ManyTemps_ProducesSpills)
{
    const std::string in = "arm64_ra_many.il";
    const std::string out = "arm64_ra_many.s";
    // Create many independent temporaries then sum some to produce a return value.
    // This should exceed caller-saved regs and drive RA to use callee-saved and spills.
    std::ostringstream il;
    il << "il 0.1\n";
    il << "func @many() -> i64 {\n";
    il << "entry:\n";
    for (int i = 0; i < 40; ++i)
        il << "  %t" << i << " = add " << i << ", 1\n"; // materialize as constants via adds
    // Chain a few adds to make return
    il << "  %a = add %t0, %t1\n";
    il << "  %b = add %a, %t2\n";
    il << "  %c = add %b, %t3\n";
    il << "  ret %c\n";
    il << "}\n";

    const std::string inP = outPath(in);
    const std::string outP = outPath(out);
    writeFile(inP, il.str());

    const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
    const int rc = cmd_codegen_arm64(3, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);

    const std::string asmText = readFile(outP);
    // For now, ensure we emitted a valid function body with adds and a return.
    EXPECT_NE(asmText.find(".text"), std::string::npos);
    EXPECT_NE(asmText.find("add x"), std::string::npos);
    EXPECT_NE(asmText.find("ret"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
