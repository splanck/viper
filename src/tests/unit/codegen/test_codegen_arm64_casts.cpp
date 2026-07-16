//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_casts.cpp
// Purpose: Verify AArch64 lowering for boolean zext/trunc and checked casts.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/internals/architecture.md
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

static std::string blSym(const std::string &name) {
#if defined(__APPLE__)
    return "bl _" + name;
#else
    return "bl " + name;
#endif
}

TEST(Arm64Casts, Zext1AndTrunc1) {
    const std::string in = outPath("arm64_cast_bool.il");
    const std::string out = outPath("arm64_cast_bool.s");
    const std::string il = "il 0.1\n"
                           "func @f(%a:i64) -> i64 {\n"
                           "entry(%a:i64):\n"
                           "  %t0 = trunc1 %a\n"
                           "  %t1 = zext1 %t0\n"
                           "  ret %t1\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect mask with 1 using mov/and (register numbers may vary)
    EXPECT_NE(asmText.find("mov x"), std::string::npos);
    EXPECT_NE(asmText.find("#1"), std::string::npos);
    EXPECT_NE(asmText.find("and x"), std::string::npos);
}

TEST(Arm64Casts, SiNarrowChk) {
    const std::string in = outPath("arm64_cast_narrow.il");
    const std::string out = outPath("arm64_cast_narrow.s");
    const std::string il = "il 0.1\n"
                           "func @f(%a:i64) -> i16 {\n"
                           "entry(%a:i64):\n"
                           "  %t0:i16 = cast.si_narrow.chk %a\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    // Expect lsl/asr pair for sign-narrow and a conditional branch to trap.
    EXPECT_NE(asmText.find("lsl x0, x0, #48"), std::string::npos);
    EXPECT_NE(asmText.find("asr x0, x0, #48"), std::string::npos);
    EXPECT_NE(asmText.find("cmp x0, x9"), std::string::npos);
    EXPECT_NE(asmText.find("b.ne L.Ltrap_cast_"), std::string::npos);
    EXPECT_NE(asmText.find("L.Ltrap_cast_"), std::string::npos);
    EXPECT_NE(asmText.find(blSym("rt_trap")), std::string::npos);
}

TEST(Arm64Casts, FpToSiRteChk) {
    const std::string in = outPath("arm64_cast_fp2si.il");
    const std::string out = outPath("arm64_cast_fp2si.s");
    const std::string il = "il 0.1\n"
                           "func @f(%a:f64) -> i64 {\n"
                           "entry(%a:f64):\n"
                           "  %t0 = cast.fp_to_si.rte.chk %a\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("frintn"), std::string::npos);
    EXPECT_NE(asmText.find("fcvtzs"), std::string::npos);
    EXPECT_NE(asmText.find("fcmp"), std::string::npos);
    EXPECT_NE(asmText.find("b.vs L.Ltrap_fp_invalid"), std::string::npos);
    EXPECT_NE(asmText.find("L.Ltrap_fp_ovf"), std::string::npos);
    EXPECT_NE(asmText.find("#4"), std::string::npos);
    EXPECT_NE(asmText.find(blSym("rt_trap_raise_error")), std::string::npos);
}

TEST(Arm64Casts, FpToUiRteChk) {
    const std::string in = outPath("arm64_cast_fp2ui.il");
    const std::string out = outPath("arm64_cast_fp2ui.s");
    const std::string il = "il 0.1\n"
                           "func @f(%a:f64) -> i64 {\n"
                           "entry(%a:f64):\n"
                           "  %t0 = cast.fp_to_ui.rte.chk %a\n"
                           "  ret %t0\n"
                           "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-S", out.c_str()};
    ASSERT_EQ(cmd_codegen_arm64(3, const_cast<char **>(argv)), 0);
    const std::string asmText = readFile(out);
    EXPECT_NE(asmText.find("frintn"), std::string::npos);
    EXPECT_NE(asmText.find("fcvtzu"), std::string::npos);
    EXPECT_NE(asmText.find("fcmp"), std::string::npos);
    EXPECT_NE(asmText.find("b.vs L.Ltrap_fp_invalid"), std::string::npos);
    EXPECT_NE(asmText.find("L.Ltrap_fp_ovf"), std::string::npos);
    EXPECT_NE(asmText.find("#4"), std::string::npos);
    EXPECT_NE(asmText.find(blSym("rt_trap_raise_error")), std::string::npos);
}

// Keep this file minimal and focused on the core cast patterns.

int main(int argc, char **argv) {
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
