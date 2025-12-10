//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_mir_dump.cpp
// Purpose: Verify MIR dump CLI flags produce expected output.
//
//===----------------------------------------------------------------------===//

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

// Helper to capture stderr during cmd_codegen_arm64 execution
class StderrCapture
{
  public:
    StderrCapture()
    {
        oldBuf_ = std::cerr.rdbuf(captured_.rdbuf());
    }

    ~StderrCapture()
    {
        std::cerr.rdbuf(oldBuf_);
    }

    std::string str() const
    {
        return captured_.str();
    }

  private:
    std::streambuf *oldBuf_;
    std::ostringstream captured_;
};

// Test: --dump-mir-before-ra produces MIR output
TEST(Arm64MIRDump, BeforeRA_ProducesMIROutput)
{
    const std::string in = outPath("mir_dump_before.il");
    const std::string out = outPath("mir_dump_before.s");
    const std::string il = "il 0.1\n"
                           "func @test_func(%a:i64, %b:i64, %c:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64, %c:i64):\n"
                           "  %t1 = add %a, %b\n"
                           "  %t2 = mul %t1, %c\n"
                           "  ret %t2\n"
                           "}\n";
    writeFile(in, il);

    std::string stderrOutput;
    {
        StderrCapture cap;
        const char *argv[] = {in.c_str(), "-S", out.c_str(), "--dump-mir-before-ra"};
        const int rc = cmd_codegen_arm64(4, const_cast<char **>(argv));
        ASSERT_EQ(rc, 0);
        stderrOutput = cap.str();
    }

    // Check for expected markers
    EXPECT_NE(stderrOutput.find("=== MIR before RA:"), std::string::npos);
    EXPECT_NE(stderrOutput.find("test_func"), std::string::npos);
    // Should see registers (either virtual %v or physical @x, depending on lowering path)
    const bool hasVirtual = (stderrOutput.find("%v") != std::string::npos);
    const bool hasPhysical = (stderrOutput.find("@x") != std::string::npos);
    EXPECT_TRUE(hasVirtual || hasPhysical);
}

// Test: --dump-mir-after-ra produces output with physical registers
TEST(Arm64MIRDump, AfterRA_ShowsPhysicalRegs)
{
    const std::string in = outPath("mir_dump_after.il");
    const std::string out = outPath("mir_dump_after.s");
    const std::string il = "il 0.1\n"
                           "func @test_func(%a:i64, %b:i64, %c:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64, %c:i64):\n"
                           "  %t1 = add %a, %b\n"
                           "  %t2 = mul %t1, %c\n"
                           "  ret %t2\n"
                           "}\n";
    writeFile(in, il);

    std::string stderrOutput;
    {
        StderrCapture cap;
        const char *argv[] = {in.c_str(), "-S", out.c_str(), "--dump-mir-after-ra"};
        const int rc = cmd_codegen_arm64(4, const_cast<char **>(argv));
        ASSERT_EQ(rc, 0);
        stderrOutput = cap.str();
    }

    // Check for expected markers
    EXPECT_NE(stderrOutput.find("=== MIR after RA:"), std::string::npos);
    EXPECT_NE(stderrOutput.find("test_func"), std::string::npos);
    // Should see physical registers (starting with @x)
    EXPECT_NE(stderrOutput.find("@x"), std::string::npos);
}

// Test: --dump-mir-full produces both before and after RA dumps
TEST(Arm64MIRDump, Full_ShowsBothPhases)
{
    const std::string in = outPath("mir_dump_full.il");
    const std::string out = outPath("mir_dump_full.s");
    const std::string il = "il 0.1\n"
                           "func @test_func(%a:i64, %b:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64):\n"
                           "  %sum = add %a, %b\n"
                           "  ret %sum\n"
                           "}\n";
    writeFile(in, il);

    std::string stderrOutput;
    {
        StderrCapture cap;
        const char *argv[] = {in.c_str(), "-S", out.c_str(), "--dump-mir-full"};
        const int rc = cmd_codegen_arm64(4, const_cast<char **>(argv));
        ASSERT_EQ(rc, 0);
        stderrOutput = cap.str();
    }

    // Should contain both before and after markers
    EXPECT_NE(stderrOutput.find("=== MIR before RA:"), std::string::npos);
    EXPECT_NE(stderrOutput.find("=== MIR after RA:"), std::string::npos);
}

// Test: MIR dump shows expected opcodes
TEST(Arm64MIRDump, ShowsExpectedOpcodes)
{
    const std::string in = outPath("mir_dump_opcodes.il");
    const std::string out = outPath("mir_dump_opcodes.s");
    const std::string il = "il 0.1\n"
                           "func @add_mul(%a:i64, %b:i64, %c:i64) -> i64 {\n"
                           "entry(%a:i64, %b:i64, %c:i64):\n"
                           "  %t1 = add %a, %b\n"
                           "  %t2 = mul %t1, %c\n"
                           "  ret %t2\n"
                           "}\n";
    writeFile(in, il);

    std::string stderrOutput;
    {
        StderrCapture cap;
        const char *argv[] = {in.c_str(), "-S", out.c_str(), "--dump-mir-after-ra"};
        const int rc = cmd_codegen_arm64(4, const_cast<char **>(argv));
        ASSERT_EQ(rc, 0);
        stderrOutput = cap.str();
    }

    // Should contain expected MIR opcodes
    EXPECT_NE(stderrOutput.find("AddRRR"), std::string::npos);
    EXPECT_NE(stderrOutput.find("MulRRR"), std::string::npos);
    EXPECT_NE(stderrOutput.find("Ret"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
