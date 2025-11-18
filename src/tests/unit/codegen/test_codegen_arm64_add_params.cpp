// File: tests/unit/codegen/test_codegen_arm64_add_params.cpp
// Purpose: Verify arm64 CLI lowers simple add of two entry parameters.

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

TEST(Arm64CLI, AddTwoParams)
{
    const std::string in = "arm64_cli_add2.il";
    const std::string out = "arm64_cli_add2.s";
    const std::string il =
        "il 0.1\n"
        "func @add2(%a:i64, %b:i64) -> i64 {\n"
        "entry(%a:i64, %b:i64):\n"
        "  %t0 = add %a, %b\n"
        "  ret %t0\n"
        "}\n";
    writeFile(in, il);

    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    const int rc = cmd_codegen_arm64(3, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);
    const std::string asmText = readFile(out);
    // Expect add using first two argument registers.
    EXPECT_NE(asmText.find("add x0, x0, x1"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}

