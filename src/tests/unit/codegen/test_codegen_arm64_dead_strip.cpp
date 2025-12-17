//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_dead_strip.cpp
// Purpose: Verify native linking dead-strips unused runtime symbols.
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include "common/RunProcess.hpp"
#include "tools/ilc/cmd_codegen_arm64.hpp"

#include <filesystem>
#include <fstream>
#include <string>

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

TEST(Arm64CLI, DeadStripsUnusedRuntimeSymbols)
{
    namespace fs = std::filesystem;
    const std::string in = outPath("arm64_dead_strip.il");
    const std::string exeOut = outPath("arm64_dead_strip_exe");
    const std::string il = "il 0.1\n"
                           "extern @rt_print_i64(i64) -> void\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  call @rt_print_i64(123)\n"
                           "  ret 0\n"
                           "}\n";
    writeFile(in, il);

    const char *argv[] = {in.c_str(), "-o", exeOut.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    ASSERT_TRUE(fs::exists(exeOut));

    const RunResult nm = run_process({"nm", "-g", exeOut});
    ASSERT_EQ(nm.exit_code, 0);
    EXPECT_NE(nm.out.find("rt_print_i64"), std::string::npos);
    EXPECT_EQ(nm.out.find("rt_input_line"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}

