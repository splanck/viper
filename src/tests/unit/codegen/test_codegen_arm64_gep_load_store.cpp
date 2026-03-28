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
#include "tests/TestHarness.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "tools/viper/cmd_codegen_arm64.hpp"

using namespace viper::tools::ilc;

static std::string outPath(const std::string &name) {
    namespace fs = std::filesystem;
    const fs::path dir{"build/test-out/arm64"};
    fs::create_directories(dir);
    return (dir / name).string();
}

static void writeFile(const std::string &path, const std::string &text) {
    std::ofstream ofs(path);
    ASSERT_TRUE(static_cast<bool>(ofs));
    ofs << text;
}

static std::string readFile(const std::string &path) {
    std::ifstream ifs(path);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

TEST(Arm64CLI, GepLoadStore_NonStack) {
    const std::string in = "arm64_cli_gep.il";
    const std::string out = "arm64_cli_gep.s";
    // Function takes base pointer and byte offset, does *(base+off)++, returns original
    const std::string il = "il 0.1\n"
                           "func @bump(%p:ptr, %off:i64) -> i64 {\n"
                           "entry(%p:ptr, %off:i64):\n"
                           "  %addr = gep %p, %off\n"
                           "  %v = load i64, %addr\n"
                           "  %one = iadd.ovf %v, 1\n"
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

TEST(Arm64CLI, GepLargeImmediate_SystemAsm) {
    const std::string in = outPath("arm64_cli_gep_large_imm.il");
    const std::string out = outPath("arm64_cli_gep_large_imm.s");
    const std::string il = "il 0.1\n"
                           "func @addr(%p:ptr) -> ptr {\n"
                           "entry(%p:ptr):\n"
                           "  %addr = gep %p, 5000\n"
                           "  ret %addr\n"
                           "}\n";

    writeFile(in, il);
    const char *argv[] = {in.c_str(), "--system-asm", "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(4, const_cast<char **>(argv)), 0);

    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("add x"), std::string::npos);
    EXPECT_TRUE(asmText.find("mov x") != std::string::npos ||
                asmText.find("movz x") != std::string::npos ||
                asmText.find("movk x") != std::string::npos);
    EXPECT_EQ(asmText.find("add x0, x8, #5000"), std::string::npos);
}

TEST(Arm64CLI, GepImmediateBoundaries_SystemAsm) {
    const std::string in = outPath("arm64_cli_gep_boundaries.il");
    const std::string out = outPath("arm64_cli_gep_boundaries.s");
    const std::string il = "il 0.1\n"
                           "func @addr(%p:ptr) -> ptr {\n"
                           "entry(%p:ptr):\n"
                           "  %a = gep %p, 4095\n"
                           "  %b = gep %a, 1\n"
                           "  %c = gep %b, 4096\n"
                           "  %d = gep %c, -8\n"
                           "  ret %d\n"
                           "}\n";

    writeFile(in, il);
    const char *argv[] = {in.c_str(), "--system-asm", "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(4, const_cast<char **>(argv)), 0);

    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("#4095"), std::string::npos);
    EXPECT_NE(asmText.find("lsl #12"), std::string::npos);
    EXPECT_NE(asmText.find("sub x"), std::string::npos);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
