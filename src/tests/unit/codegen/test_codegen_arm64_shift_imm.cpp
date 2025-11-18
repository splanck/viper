// File: tests/unit/codegen/test_codegen_arm64_shift_imm.cpp
// Purpose: Verify shl/lshr/ashr immediate lowering for param0 and param1.

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

TEST(Arm64CLI, ShiftImmParam0Param1)
{
    struct Case { const char *op; const char *expect0; const char *expect1op; int imm0; int imm1; } cases[] = {
        {"shl",  "lsl x0, x0, #4", "lsl x0, x0, #4", 4, 4},
        {"lshr", "lsr x0, x0, #5", "lsr x0, x0, #5", 5, 5},
        {"ashr", "asr x0, x0, #6", "asr x0, x0, #6", 6, 6},
    };
    for (const auto &c : cases)
    {
        // param0
        {
            std::string in = std::string("arm64_") + c.op + "_p0.il";
            std::string out = std::string("arm64_") + c.op + "_p0.s";
            std::string il = std::string(
                "il 0.1\n"
                "func @f(%a:i64, %b:i64) -> i64 {\n"
                "entry(%a:i64, %b:i64):\n"
                "  %t0 = ") + c.op + " %a, " + std::to_string(c.imm0) + "\n  ret %t0\n}\n";
            const std::string inP = outPath(in);
            const std::string outP = outPath(out);
            writeFile(inP, il);
            const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
            ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
            const std::string asmText = readFile(outP);
            EXPECT_NE(asmText.find(c.expect0), std::string::npos);
        }
        // param1
        {
            std::string in = std::string("arm64_") + c.op + "_p1.il";
            std::string out = std::string("arm64_") + c.op + "_p1.s";
            std::string il = std::string(
                "il 0.1\n"
                "func @f(%a:i64, %b:i64) -> i64 {\n"
                "entry(%a:i64, %b:i64):\n"
                "  %t0 = ") + c.op + " %b, " + std::to_string(c.imm1) + "\n  ret %t0\n}\n";
            const std::string inP = outPath(in);
            const std::string outP = outPath(out);
            writeFile(inP, il);
            const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
            ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
            const std::string asmText = readFile(outP);
            EXPECT_NE(asmText.find("mov x0, x1"), std::string::npos);
            EXPECT_NE(asmText.find(c.expect1op), std::string::npos);
        }
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
