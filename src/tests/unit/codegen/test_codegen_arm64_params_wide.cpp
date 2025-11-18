// File: tests/unit/codegen/test_codegen_arm64_params_wide.cpp
// Purpose: Verify rr/ri lowering with params beyond x1 using scratch moves.

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

TEST(Arm64CLI, ParamsBeyondX1)
{
    // rr: add %c(x2), %a(x0) → expect moves via x9 then add
    {
        const std::string in = "arm64_wide_rr.il";
        const std::string out = "arm64_wide_rr.s";
        const std::string il =
            "il 0.1\n"
            "func @f(%a:i64, %b:i64, %c:i64) -> i64 {\n"
            "entry(%a:i64, %b:i64, %c:i64):\n"
            "  %t0 = add %c, %a\n"
            "  ret %t0\n"
            "}\n";
        writeFile(in, il);
        const char *argv[] = {in.c_str(), "-S", out.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(out);
        // Expected sequence: mov x9, x0; mov x0, x2; mov x1, x9; add x0, x0, x1
        EXPECT_NE(asmText.find("mov x9, x0"), std::string::npos);
        EXPECT_NE(asmText.find("mov x0, x2"), std::string::npos);
        EXPECT_NE(asmText.find("mov x1, x9"), std::string::npos);
        EXPECT_NE(asmText.find("add x0, x0, x1"), std::string::npos);
    }

    // ri: sub %d(x3), 7 → expect mov x0, x3; sub x0, x0, #7
    {
        const std::string in = "arm64_wide_ri.il";
        const std::string out = "arm64_wide_ri.s";
        const std::string il =
            "il 0.1\n"
            "func @g(%a:i64, %b:i64, %c:i64, %d:i64) -> i64 {\n"
            "entry(%a:i64, %b:i64, %c:i64, %d:i64):\n"
            "  %t0 = sub %d, 7\n"
            "  ret %t0\n"
            "}\n";
        writeFile(in, il);
        const char *argv[] = {in.c_str(), "-S", out.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(out);
        EXPECT_NE(asmText.find("mov x0, x3"), std::string::npos);
        EXPECT_NE(asmText.find("sub x0, x0, #7"), std::string::npos);
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}

