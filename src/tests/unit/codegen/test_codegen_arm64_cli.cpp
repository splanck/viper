// File: tests/unit/codegen/test_codegen_arm64_cli.cpp
// Purpose: Smoke test for `ilc codegen arm64 -S` handling of `ret 0`.

#include "tests/unit/GTestStub.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include "tools/ilc/cmd_codegen_arm64.hpp"

using namespace viper::tools::ilc;

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
    const std::string il =
        "il 0.1\n\n"
        "func @main() -> i64 {\n"
        "entry:\n"
        "  ret 0\n"
        "}\n";
    writeFile(in, il);

    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    const int rc = cmd_codegen_arm64(3, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);
    const std::string asmText = readFile(out);
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

