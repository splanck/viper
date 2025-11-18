// File: tests/unit/codegen/test_codegen_arm64_ovf.cpp
// Purpose: Verify iadd.ovf/isub.ovf/imul.ovf rr lowering on two entry params.

#include "tests/unit/GTestStub.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>

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

TEST(Arm64CLI, OverflowVariantsRR)
{
    struct Case { const char *op; const char *expect; } cases[] = {
        {"iadd.ovf", "add x0, x0, x1"},
        {"isub.ovf", "sub x0, x0, x1"},
        {"imul.ovf", "mul x0, x0, x1"},
    };
    for (const auto &c : cases)
    {
        std::string in = std::string("arm64_ovf_") + c.op + ".il";
        std::string out = std::string("arm64_ovf_") + c.op + ".s";
        std::string il = std::string(
            "il 0.1\n"
            "func @f(%a:i64, %b:i64) -> i64 {\n"
            "entry(%a:i64, %b:i64):\n"
            "  %t0 = ") + c.op + " %a, %b\n  ret %t0\n}\n";
        const std::string inP = outPath(in);
        const std::string outP = outPath(out);
        writeFile(inP, il);
        const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
        ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
        const std::string asmText = readFile(outP);
        EXPECT_NE(asmText.find(c.expect), std::string::npos);
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
