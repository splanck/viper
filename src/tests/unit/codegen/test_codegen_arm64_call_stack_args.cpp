// File: tests/unit/codegen/test_codegen_arm64_call_stack_args.cpp
// Purpose: Verify CLI marshals >8 integer args by using stack slots.

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

TEST(Arm64CLI, CallWithStackArgs)
{
    const std::string in = outPath("arm64_call_stack.il");
    const std::string out = outPath("arm64_call_stack.s");
    const std::string il = "il 0.1\n"
                           "extern @h(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64) -> i64\n"
                           "func @f(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %t0 = call @h(%a, %b, 3, 4, 5, 6, 7, 8, 9, 10)\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect outgoing stack area allocation (16 bytes for two extra args)
    EXPECT_NE(asmText.find("sub sp, sp, #16"), std::string::npos);
    // Expect stores of the last two args to [sp, #0] and [sp, #8]
    EXPECT_NE(asmText.find("str x"), std::string::npos);
    EXPECT_NE(asmText.find("[sp, #0]"), std::string::npos);
    EXPECT_NE(asmText.find("[sp, #8]"), std::string::npos);
    // Call and stack deallocation
    EXPECT_NE(asmText.find("bl h"), std::string::npos);
    EXPECT_NE(asmText.find("add sp, sp, #16"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
