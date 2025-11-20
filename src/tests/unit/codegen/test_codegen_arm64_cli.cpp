// File: tests/unit/codegen/test_codegen_arm64_cli.cpp
// Purpose: Smoke test for `ilc codegen arm64 -S` handling of `ret 0`.

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

TEST(Arm64CLI, RetZeroEmitsMovX0Zero)
{
    const std::string in = "arm64_cli_ret0.il";
    const std::string out = "arm64_cli_ret0.s";
    const std::string il = "il 0.1\n\n"
                           "func @main() -> i64 {\n"
                           "entry:\n"
                           "  ret 0\n"
                           "}\n";
    const std::string inP = outPath(in);
    const std::string outP = outPath(out);
    writeFile(inP, il);

    const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
    const int rc = cmd_codegen_arm64(3, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);
    const std::string asmText = readFile(outP);
    // Expect prologue, mov x0, #0 before epilogue ret
    EXPECT_NE(asmText.find("stp x29, x30"), std::string::npos);
    EXPECT_NE(asmText.find("mov x0, #0"), std::string::npos);
    EXPECT_NE(asmText.find("ldp x29, x30"), std::string::npos);
    EXPECT_NE(asmText.find("ret"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
