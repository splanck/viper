// File: tests/unit/codegen/test_codegen_arm64_bitwise.cpp
// Purpose: Verify and/or/xor lowering on two entry params.

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

TEST(Arm64CLI, BitwiseRR)
{
    struct Case { const char *op; const char *expect; } cases[] = {
        {"and", "and x0, x0, x1"},
        {"or",  "orr x0, x0, x1"},
        {"xor", "eor x0, x0, x1"},
    };
    for (const auto &c : cases)
    {
        std::string in = std::string("arm64_bit_") + c.op + ".il";
        std::string out = std::string("arm64_bit_") + c.op + ".s";
        std::string il = std::string(
            "il 0.1\n"
            "func @f(%a:i64, %b:i64) -> i64 {\n"
            "entry(%a:i64, %b:i64):\n"
            "  %t0 = ") + c.op + " %a, %b\n  ret %t0\n}\n";
        writeFile(in, il);
        const char *argv[] = {in.c_str(), "-S", out.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(out);
        EXPECT_NE(asmText.find(c.expect), std::string::npos);
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
