//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/unit/codegen/test_codegen_arm64_mixed_call_f64.cpp
// Purpose: Verify native codegen correctly handles runtime calls with mixed
//          GPR (str/ptr/i64) and FPR (f64) arguments.
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "tools/viper/cmd_codegen_arm64.hpp"

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

/// @brief Call rt_parse_num_or("3", 99.0) — should parse "3" and return 3.0.
/// Verifies f64 return value is correctly read from D0.
TEST(Arm64MixedCallF64, ParseNumOrValidString)
{
    const std::string in = outPath("arm64_numor_valid.il");
    const std::string il =
        "il 0.1\n"
        "extern @rt_parse_num_or(str, f64) -> f64\n"
        "global const str @.Lnum = \"3\"\n"
        "func @main() -> i64 {\n"
        "entry:\n"
        "  %s = const_str @.Lnum\n"
        "  %def = sitofp 99\n"
        "  %r = call @rt_parse_num_or(%s, %def)\n"
        "  %i = fptosi %r\n"
        "  ret %i\n"
        "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    // "3" parses to 3.0, fptosi → 3
    ASSERT_EQ(rc, 3);
}

/// @brief Call rt_parse_num_or("abc", 42.0) — parse fails, returns default 42.0.
/// This specifically tests that the f64 default_value argument is correctly
/// passed in D0 (FPR) rather than X1 (GPR) on AArch64.
TEST(Arm64MixedCallF64, ParseNumOrInvalidStringReturnsDefault)
{
    const std::string in = outPath("arm64_numor_default.il");
    const std::string il =
        "il 0.1\n"
        "extern @rt_parse_num_or(str, f64) -> f64\n"
        "global const str @.Lfail = \"abc\"\n"
        "func @main() -> i64 {\n"
        "entry:\n"
        "  %s = const_str @.Lfail\n"
        "  %def = sitofp 42\n"
        "  %r = call @rt_parse_num_or(%s, %def)\n"
        "  %i = fptosi %r\n"
        "  ret %i\n"
        "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    // "abc" fails to parse, returns default 42.0, fptosi → 42
    ASSERT_EQ(rc, 42);
}

/// @brief Call rt_parse_num_or("", 7.0) — empty string, returns default 7.0.
/// Tests default value with empty string input.
TEST(Arm64MixedCallF64, ParseNumOrEmptyStringReturnsDefault)
{
    const std::string in = outPath("arm64_numor_empty.il");
    const std::string il =
        "il 0.1\n"
        "extern @rt_parse_num_or(str, f64) -> f64\n"
        "global const str @.Lempty = \"\"\n"
        "func @main() -> i64 {\n"
        "entry:\n"
        "  %s = const_str @.Lempty\n"
        "  %def = sitofp 7\n"
        "  %r = call @rt_parse_num_or(%s, %def)\n"
        "  %i = fptosi %r\n"
        "  ret %i\n"
        "}\n";
    writeFile(in, il);
    const char *argv[] = {in.c_str(), "-run-native"};
    const int rc = cmd_codegen_arm64(2, const_cast<char **>(argv));
    // Empty string fails to parse, returns default 7.0, fptosi → 7
    ASSERT_EQ(rc, 7);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
