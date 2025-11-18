// File: tests/unit/codegen/test_codegen_arm64_ret_param.cpp
// Purpose: Verify returning parameters lowers to correct moves/no-op.

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

TEST(Arm64CLI, RetParam)
{
    // Return param0: no mov expected (x0 already contains arg0)
    {
        const std::string in = "arm64_ret_p0.il";
        const std::string out = "arm64_ret_p0.s";
        const std::string il =
            "il 0.1\n"
            "func @id0(%a:i64, %b:i64) -> i64 {\n"
            "entry(%a:i64, %b:i64):\n"
            "  ret %a\n"
            "}\n";
        writeFile(in, il);
        const char *argv[] = {in.c_str(), "-S", out.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(out);
        EXPECT_EQ(asmText.find("mov x0, x1"), std::string::npos);
    }

    // Return param1: expect mov x0, x1
    {
        const std::string in = "arm64_ret_p1.il";
        const std::string out = "arm64_ret_p1.s";
        const std::string il =
            "il 0.1\n"
            "func @id1(%a:i64, %b:i64) -> i64 {\n"
            "entry(%a:i64, %b:i64):\n"
            "  ret %b\n"
            "}\n";
        writeFile(in, il);
        const char *argv[] = {in.c_str(), "-S", out.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(out);
        EXPECT_NE(asmText.find("mov x0, x1"), std::string::npos);
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}

