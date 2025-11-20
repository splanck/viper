// File: tests/unit/codegen/test_codegen_arm64_icmp_imm.cpp
// Purpose: Verify integer compares against immediates using cmp #imm + cset.

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

TEST(Arm64CLI, ICmpImm)
{
    // icmp_eq %a, 42
    {
        const std::string in = "arm64_icmp_imm_eq.il";
        const std::string out = "arm64_icmp_imm_eq.s";
        const std::string il = "il 0.1\n"
                               "func @f(%a:i64) -> i64 {\n"
                               "entry(%a:i64):\n"
                               "  %t0 = icmp_eq %a, 42\n"
                               "  ret %t0\n"
                               "}\n";
        const std::string inP = outPath(in);
        const std::string outP = outPath(out);
        writeFile(inP, il);
        const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(outP);
        EXPECT_NE(asmText.find("cmp x0, #42"), std::string::npos);
        EXPECT_NE(asmText.find("cset x0, eq"), std::string::npos);
    }
    // scmp_lt %b, -7
    {
        const std::string in = "arm64_icmp_imm_slt.il";
        const std::string out = "arm64_icmp_imm_slt.s";
        const std::string il = "il 0.1\n"
                               "func @g(%a:i64, %b:i64) -> i64 {\n"
                               "entry(%a:i64, %b:i64):\n"
                               "  %t0 = scmp_lt %b, -7\n"
                               "  ret %t0\n"
                               "}\n";
        const std::string inP = outPath(in);
        const std::string outP = outPath(out);
        writeFile(inP, il);
        const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(outP);
        EXPECT_NE(asmText.find("mov x0, x1"), std::string::npos);
        EXPECT_NE(asmText.find("cmp x0, #-7"), std::string::npos);
        EXPECT_NE(asmText.find("cset x0, lt"), std::string::npos);
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
