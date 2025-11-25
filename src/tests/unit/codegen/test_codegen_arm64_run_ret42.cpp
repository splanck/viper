//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_run_ret42.cpp
// Purpose: Verify `ilc codegen arm64 --run-native` returns function exit code.
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include <filesystem>
#include <fstream>
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

TEST(Arm64CLI, RunNative_Ret42)
{
    const std::string in = outPath("ret42.il");
    const std::string il =
        "il 0.1\n"
        "func @main() -> i64 {\n"
        "entry:\n"
        "  ret 42\n"
        "}\n";
    {
        std::ofstream ofs(in);
        ASSERT_TRUE(static_cast<bool>(ofs));
        ofs << il;
    }
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    ASSERT_EQ(rc, 42);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
