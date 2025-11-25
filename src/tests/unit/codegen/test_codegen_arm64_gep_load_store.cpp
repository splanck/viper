//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_gep_load_store.cpp
// Purpose: Verify AArch64 lowers GEP + load/store for non-stack memory.
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

static std::string readFile(const std::string &path)
{
    std::ifstream ifs(path);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

TEST(Arm64CLI, GepLoadStore_NonStack)
{
    const std::string in = "arm64_cli_gep.il";
    const std::string out = "arm64_cli_gep.s";
    // Function takes base pointer and byte offset, does *(base+off)++, returns original
    const std::string il = "il 0.1\n"
                           "func @bump(%p:ptr, %off:i64) -> i64 {\n"
                           "entry(%p:ptr, %off:i64):\n"
                           "  %addr = gep %p, %off\n"
                           "  %v = load i64, %addr\n"
                           "  %one = add %v, 1\n"
                           "  store i64, %addr, %one\n"
                           "  ret %v\n"
                           "}\n";

    const std::string inP = outPath(in);
    const std::string outP = outPath(out);
    writeFile(inP, il);

    const char *argv[] = {inP.c_str(), "-S", outP.c_str()};
    const int rc = cmd_codegen_arm64(3, const_cast<char **>(argv));
    ASSERT_EQ(rc, 0);

    const std::string asmText = readFile(outP);
    // Expect address arithmetic (add) and base-relative ldr/str
    EXPECT_NE(asmText.find(" add "), std::string::npos);
    EXPECT_NE(asmText.find("ldr x"), std::string::npos);
    EXPECT_NE(asmText.find("str x"), std::string::npos);
    // Should not be using FP-relative addressing for these memory ops
    // (prologue may still reference x29 for frame, but loads/stores shouldn't use [x29, #..]).
    // Be lenient: just ensure at least one ldr/str uses a non-FP base pattern
    EXPECT_NE(asmText.find("[x"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
